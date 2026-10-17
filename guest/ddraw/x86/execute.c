/*
 * execute buffers of IDirect3DDevice, read from d3dim.dll of build 26300:
 * a stream of instructions the device interprets at Execute. vertices are
 * transformed and lit here into screen space, as windows' front end
 * does, primitives cut against the view volume before they are drawn,
 * and the clip status and extents kept for the stream to read back.
 */
#include "d3d_private.h"

static const IDirect3DExecuteBufferVtbl buffer_vtbl;

D3DEXECBUF *D3dExecuteBufferFrom(void *iface)
{
    IDirect3DExecuteBuffer *i = iface;
    return i && i->lpVtbl == &buffer_vtbl ? CONTAINING_RECORD(i, D3DEXECBUF, iface) : nullptr;
}

/* system memory of the size asked, whatever the caps say. */
HRESULT D3dCreateExecuteBuffer(D3DDEV *dev, const D3DEXECUTEBUFFERDESC *desc, IDirect3DExecuteBuffer **out)
{
    DWORD max = dev->legacy->sw.dwMaxBufferSize;
    if (!desc->dwFlags || !desc->dwBufferSize || (max && desc->dwBufferSize > max))
        return DDERR_INVALIDPARAMS;
    D3DEXECBUF *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return DDERR_OUTOFMEMORY;
    b->memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, desc->dwBufferSize);
    if (!b->memory) {
        HeapFree(GetProcessHeap(), 0, b);
        return D3DERR_EXECUTE_CREATE_FAILED;
    }
    b->iface.lpVtbl = &buffer_vtbl;
    b->ref = 1;
    b->dev = dev;
    b->size = desc->dwBufferSize;
    InsertHeadList(&dev->legacy->buffers, &b->links);
    *out = &b->iface;
    return D3D_OK;
}

/* a buffer outliving its device draws nothing more. */
void D3dExecuteBufferDeviceGone(D3DEXECBUF *b)
{
    RemoveEntryList(&b->links);
    InitializeListHead(&b->links);
    b->dev = nullptr;
}

/* ------------------------------------------------------------------ IDirect3DExecuteBuffer */

static HRESULT STDMETHODCALLTYPE buffer_query(IDirect3DExecuteBuffer *This, REFIID riid, void **out)
{
    if (!out)
        return DDERR_INVALIDPARAMS;
    *out = nullptr;
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return DDERR_INVALIDOBJECT;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirect3DExecuteBuffer))
        return E_NOINTERFACE;
    DdEnter();
    b->ref++;
    DdLeave(D3D_OK);
    *out = &b->iface;
    return D3D_OK;
}

static ULONG STDMETHODCALLTYPE buffer_add_ref(IDirect3DExecuteBuffer *This)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)++b->ref;
    DdLeave(D3D_OK);
    return ref;
}

static ULONG STDMETHODCALLTYPE buffer_release(IDirect3DExecuteBuffer *This)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return 0;
    DdEnter();
    ULONG ref = (ULONG)--b->ref;
    if (!ref) {
        RemoveEntryList(&b->links);
        HeapFree(GetProcessHeap(), 0, b->memory);
        b->iface.lpVtbl = nullptr;
        HeapFree(GetProcessHeap(), 0, b);
    }
    DdLeave(D3D_OK);
    return ref;
}

static HRESULT STDMETHODCALLTYPE buffer_initialize(IDirect3DExecuteBuffer *This, LPDIRECT3DDEVICE device,
                                                   LPD3DEXECUTEBUFFERDESC desc)
{
    return DDERR_ALREADYINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE buffer_lock(IDirect3DExecuteBuffer *This, LPD3DEXECUTEBUFFERDESC desc)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return DDERR_INVALIDOBJECT;
    if (!desc || desc->dwSize != sizeof(*desc))
        return DDERR_INVALIDPARAMS;
    DdEnter();
    if (b->locked)
        return DdLeave(D3DERR_EXECUTE_LOCKED);
    b->locked = TRUE;
    b->locker = GetCurrentProcessId();
    *desc = (D3DEXECUTEBUFFERDESC){sizeof(*desc), D3DDEB_LPDATA, 0, b->size, b->memory};
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_unlock(IDirect3DExecuteBuffer *This)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return DDERR_INVALIDOBJECT;
    DdEnter();
    if (!b->locked)
        return DdLeave(D3DERR_EXECUTE_NOT_LOCKED);
    if (b->locker != GetCurrentProcessId())
        return DdLeave(DDERR_INVALIDPARAMS);
    b->locked = FALSE;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_set_execute_data(IDirect3DExecuteBuffer *This, LPD3DEXECUTEDATA data)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return DDERR_INVALIDOBJECT;
    if (!data || data->dwSize != sizeof(*data))
        return DDERR_INVALIDPARAMS;
    DdEnter();
    b->data = *data;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_execute_data(IDirect3DExecuteBuffer *This, LPD3DEXECUTEDATA data)
{
    D3DEXECBUF *b = D3dExecuteBufferFrom(This);
    if (!b)
        return DDERR_INVALIDOBJECT;
    if (!data)
        return DDERR_INVALIDPARAMS;
    DdEnter();
    *data = b->data;
    return DdLeave(D3D_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_validate(IDirect3DExecuteBuffer *This, LPDWORD offset,
                                                 LPD3DVALIDATECALLBACK callback, LPVOID context, DWORD reserved)
{
    return D3dExecuteBufferFrom(This) ? E_NOTIMPL : DDERR_INVALIDOBJECT;
}

static HRESULT STDMETHODCALLTYPE buffer_optimize(IDirect3DExecuteBuffer *This, DWORD flags)
{
    if (!D3dExecuteBufferFrom(This))
        return DDERR_INVALIDOBJECT;
    return flags ? DDERR_INVALIDPARAMS : E_NOTIMPL;
}

static const IDirect3DExecuteBufferVtbl buffer_vtbl = {
    buffer_query,           buffer_add_ref,          buffer_release,  buffer_initialize, buffer_lock,
    buffer_unlock,          buffer_set_execute_data, buffer_get_execute_data, buffer_validate, buffer_optimize,
};

/* ------------------------------------------------------------------ the transformed vertices */

typedef struct RUN {
    D3DDEV *dev;
    D3DEXECBUF *b;
    BOOL clipped;           /* clipping, as no COPY has run yet in a clipped Execute */
    const D3DRECT *pick;
    const BYTE *start;      /* the first instruction, which pick records count from */
    DWORD count;            /* vertices of the stream, after which clipped ones are added */
    DWORD extra;
    WORD *indices;
    DWORD index_count, index_capacity;
} RUN;

/* room for the stream's vertices and those clipping makes. */
static BOOL reserve(D3DLEGACY *l, DWORD n)
{
    if (n <= l->tl_capacity)
        return TRUE;
    DWORD capacity = n < 256 ? 256 : n + n / 2;
    if (capacity > 0x10000)
        capacity = 0x10000;
    if (n > capacity)
        return FALSE;
    BYTE *tl = l->tl ? HeapReAlloc(GetProcessHeap(), 0, l->tl, (SIZE_T)capacity * sizeof(D3DTLVERTEX))
                     : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)capacity * sizeof(D3DTLVERTEX));
    if (tl)
        l->tl = tl;
    float *clip = l->tl_clip ? HeapReAlloc(GetProcessHeap(), 0, l->tl_clip, (SIZE_T)capacity * 4 * sizeof(float))
                             : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)capacity * 4 * sizeof(float));
    if (clip)
        l->tl_clip = clip;
    WORD *codes = l->tl_codes ? HeapReAlloc(GetProcessHeap(), 0, l->tl_codes, (SIZE_T)capacity * sizeof(WORD))
                              : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)capacity * sizeof(WORD));
    if (codes)
        l->tl_codes = codes;
    if (!tl || !clip || !codes)
        return FALSE;
    l->tl_capacity = capacity;
    return TRUE;
}

static D3DTLVERTEX *tl_at(D3DLEGACY *l, DWORD i)
{
    return (D3DTLVERTEX *)l->tl + i;
}

static WORD clip_code(const float *c)
{
    WORD code = 0;
    if (c[0] < -c[3])
        code |= D3DCLIP_LEFT;
    if (c[0] > c[3])
        code |= D3DCLIP_RIGHT;
    if (c[1] > c[3])
        code |= D3DCLIP_TOP;
    if (c[1] < -c[3])
        code |= D3DCLIP_BOTTOM;
    if (c[2] < 0.0f)
        code |= D3DCLIP_FRONT;
    if (c[2] > c[3])
        code |= D3DCLIP_BACK;
    return code;
}

/* a clip space position on the target, through the viewport the device has. */
static void to_screen(D3DDEV *dev, const float *c, D3DTLVERTEX *v)
{
    const D3DVIEWPORT7 *vp = &dev->viewport;
    float rhw = c[3] != 0.0f ? 1.0f / c[3] : 0.0f;
    v->sx = (float)vp->dwX + (c[0] * rhw + 1.0f) * 0.5f * (float)vp->dwWidth;
    v->sy = (float)vp->dwY + (1.0f - c[1] * rhw) * 0.5f * (float)vp->dwHeight;
    v->sz = c[2] * rhw;
    v->rhw = rhw;
}

static void grow_extents(D3DLEGACY *l, const D3DTLVERTEX *v)
{
    if (!(v->sx == v->sx) || !(v->sy == v->sy))
        return;
    l->extents[0] = v->sx < l->extents[0] ? v->sx : l->extents[0];
    l->extents[1] = v->sy < l->extents[1] ? v->sy : l->extents[1];
    l->extents[2] = v->sx > l->extents[2] ? v->sx : l->extents[2];
    l->extents[3] = v->sy > l->extents[3] ? v->sy : l->extents[3];
}

static HRESULT process_vertices(RUN *run, const D3DPROCESSVERTICES *p)
{
    D3DDEV *dev = run->dev;
    D3DLEGACY *l = dev->legacy;
    DWORD op = p->dwFlags & D3DPROCESSVERTICES_OPMASK, count = p->dwCount;
    l->stats.dwVerticesProcessed += count;
    /* the source counts in vertices from the start of the buffer, as windows ignores the vertex offset. */
    SIZE_T src_offset = (SIZE_T)p->wStart * sizeof(D3DTLVERTEX);
    if (src_offset >= run->b->size)
        return D3D_OK;
    if (count > (run->b->size - src_offset) / sizeof(D3DTLVERTEX))
        count = (DWORD)((run->b->size - src_offset) / sizeof(D3DTLVERTEX));
    if (p->wDest >= run->count)
        return D3D_OK;
    if (count > run->count - p->wDest)
        count = run->count - p->wDest;
    const BYTE *src = run->b->memory + src_offset;
    D3DTLVERTEX *out = tl_at(l, p->wDest);
    float *clip = l->tl_clip + (SIZE_T)p->wDest * 4;
    WORD *codes = l->tl_codes + p->wDest;
    BOOL ramp = IsEqualGUID(&dev->guid, &IID_IDirect3DRampDevice);
    if (op == D3DPROCESSVERTICES_COPY) {
        memcpy(out, src, (SIZE_T)count * sizeof(D3DTLVERTEX));
        memset(codes, 0, count * sizeof(WORD));
        for (DWORD i = 0; i < count; i++) {
            grow_extents(l, &out[i]);
            if (ramp) {
                out[i].color = D3dRampConvert(dev, out[i].color, out[i].specular);
                out[i].specular &= 0xFF000000;
            }
        }
        l->status &= 0xFF000FFF;
        run->clipped = FALSE;
        return D3D_OK;
    }
    D3DMATRIX wv = D3dMultiply(&dev->transforms[D3DTRANSFORMSTATE_WORLD], &dev->transforms[D3DTRANSFORMSTATE_VIEW]);
    D3DMATRIX all = D3dMultiply(&wv, &dev->transforms[D3DTRANSFORMSTATE_PROJECTION]);
    all = D3dMultiply(&all, &l->clip_map);
    BOOL lit = op == D3DPROCESSVERTICES_TRANSFORMLIGHT, material = l->light_states[D3DLIGHTSTATE_MATERIAL] != 0;
    BOOL fog = dev->rs[D3DRENDERSTATE_FOGENABLE] && l->light_states[D3DLIGHTSTATE_FOGMODE] != D3DFOG_NONE;
    WORD uni = 0, every = 0xFFF;
    for (DWORD i = 0; i < count; i++) {
        const float *in = (const float *)(src + (SIZE_T)i * sizeof(D3DTLVERTEX));
        float *c = clip + (SIZE_T)i * 4;
        c[0] = in[0] * all._11 + in[1] * all._21 + in[2] * all._31 + all._41;
        c[1] = in[0] * all._12 + in[1] * all._22 + in[2] * all._32 + all._42;
        c[2] = in[0] * all._13 + in[1] * all._23 + in[2] * all._33 + all._43;
        c[3] = in[0] * all._14 + in[1] * all._24 + in[2] * all._34 + all._44;
        codes[i] = run->clipped ? clip_code(c) : 0;
        uni |= codes[i];
        every &= codes[i];
        to_screen(dev, c, &out[i]);
        grow_extents(l, &out[i]);
        if (!lit) {
            memcpy(&out[i].color, in + 4, 8);
            if (ramp) {
                out[i].color = D3dRampConvert(dev, out[i].color, out[i].specular);
                out[i].specular &= 0xFF000000;
            }
        } else if (!material) {
            out[i].color = 0xFFFFFFFF;
            out[i].specular = 0;
        }
        memcpy(&out[i].tu, in + 6, 8);
        if (fog && !(lit && material)) {
            float z = in[0] * wv._13 + in[1] * wv._23 + in[2] * wv._33 + wv._43;
            out[i].specular = (out[i].specular & 0x00FFFFFF) | D3dLegacyFog(dev, z) << 24;
        }
    }
    if (lit && material) {
        D3dLegacyLight(dev, src, D3DFVF_VERTEX, count, (BYTE *)&out->color, sizeof(D3DTLVERTEX));
        if (fog)
            for (DWORD i = 0; i < count; i++) {
                const float *in = (const float *)(src + (SIZE_T)i * sizeof(D3DTLVERTEX));
                float z = in[0] * wv._13 + in[1] * wv._23 + in[2] * wv._33 + wv._43;
                out[i].specular = (out[i].specular & 0x00FFFFFF) | D3dLegacyFog(dev, z) << 24;
            }
    }
    if (run->clipped && count)
        l->status = (l->status | uni) & (((DWORD)every << 12) | 0xFF000FFF);
    else
        l->status &= 0xFF000FFF;
    return D3D_OK;
}

/* ------------------------------------------------------------------ clipping */

/* a vertex between two others, at t from the first, in clip space and in every attribute. */
static DWORD lerp_vertex(RUN *run, DWORD a, DWORD b, float t)
{
    D3DLEGACY *l = run->dev->legacy;
    DWORD n = run->count + run->extra;
    if (!reserve(l, n + 1))
        return a;
    run->extra++;
    const float *ca = l->tl_clip + (SIZE_T)a * 4, *cb = l->tl_clip + (SIZE_T)b * 4;
    float *c = l->tl_clip + (SIZE_T)n * 4;
    for (int i = 0; i < 4; i++)
        c[i] = ca[i] + (cb[i] - ca[i]) * t;
    const D3DTLVERTEX *va = tl_at(l, a), *vb = tl_at(l, b);
    D3DTLVERTEX *v = tl_at(l, n);
    DWORD colors[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        DWORD x = k ? va->specular : va->color, y = k ? vb->specular : vb->color;
        for (int s = 0; s < 32; s += 8) {
            float fa = (float)(x >> s & 0xFF), fb = (float)(y >> s & 0xFF);
            colors[k] |= (DWORD)(fa + (fb - fa) * t + 0.5f) << s;
        }
    }
    v->color = colors[0];
    v->specular = colors[1];
    v->tu = va->tu + (vb->tu - va->tu) * t;
    v->tv = va->tv + (vb->tv - va->tv) * t;
    to_screen(run->dev, c, v);
    l->tl_codes[n] = 0;
    return n;
}

/* the signed distance to a plane of the view volume, inside when not negative. */
static float plane_distance(const float *c, int plane)
{
    switch (plane) {
    case 0:
        return c[3] + c[0];
    case 1:
        return c[3] - c[0];
    case 2:
        return c[3] - c[1];
    case 3:
        return c[3] + c[1];
    case 4:
        return c[2];
    default:
        return c[3] - c[2];
    }
}

/* a polygon cut by each plane its vertices cross, sutherland and hodgman's way. */
static DWORD clip_polygon(RUN *run, DWORD *in, DWORD n, WORD codes)
{
    D3DLEGACY *l = run->dev->legacy;
    DWORD buffer[2][16], *out = buffer[0];
    for (int plane = 0; plane < 6 && n; plane++) {
        if (!(codes & (1u << plane)))
            continue;
        DWORD m = 0;
        for (DWORD i = 0; i < n && m < 14; i++) {
            DWORD a = in[i], b = in[(i + 1) % n];
            float da = plane_distance(l->tl_clip + (SIZE_T)a * 4, plane);
            float db = plane_distance(l->tl_clip + (SIZE_T)b * 4, plane);
            if (da >= 0.0f)
                out[m++] = a;
            if ((da >= 0.0f) != (db >= 0.0f))
                out[m++] = lerp_vertex(run, a, b, da / (da - db));
        }
        memcpy(in, out, m * sizeof(DWORD));
        n = m;
        out = out == buffer[0] ? buffer[1] : buffer[0];
    }
    return n;
}

static BOOL add_index(RUN *run, DWORD i)
{
    if (run->index_count == run->index_capacity) {
        DWORD capacity = run->index_capacity ? run->index_capacity * 2 : 1024;
        WORD *indices = run->indices ? HeapReAlloc(GetProcessHeap(), 0, run->indices, capacity * sizeof(WORD))
                                     : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WORD));
        if (!indices)
            return FALSE;
        run->indices = indices;
        run->index_capacity = capacity;
    }
    run->indices[run->index_count++] = (WORD)i;
    return TRUE;
}

static HRESULT flush(RUN *run, D3DPRIMITIVETYPE type)
{
    HRESULT hr = D3D_OK;
    if (run->index_count)
        hr = D3dLegacyRender(run->dev, type, D3DFVF_TLVERTEX, run->dev->legacy->tl, run->count + run->extra,
                             run->indices, run->index_count, 0);
    run->index_count = 0;
    run->extra = 0;
    return hr;
}

/* ------------------------------------------------------------------ picking */

/* whether the pick pixel falls in a clockwise screen triangle, and the depth there. */
static BOOL pick_test(const D3DTLVERTEX *a, const D3DTLVERTEX *b, const D3DTLVERTEX *c, LONG px, LONG py, float *z)
{
    float ay = (float)(LONG)a->sy, by = (float)(LONG)b->sy, cy = (float)(LONG)c->sy;
    if (ay == by && by == cy)
        return FALSE;
    float area = (b->sx - a->sx) * (cy - ay) - (by - ay) * (c->sx - a->sx);
    if (area <= 0.0f)
        return FALSE;
    float y = (float)py, x = (float)px;
    float top = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    float bottom = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    if (y < top || y >= bottom)
        return FALSE;
    /* the span of the scanline between the edges it crosses. */
    const float xs[3] = {a->sx, b->sx, c->sx}, ys[3] = {ay, by, cy};
    float left = 3.4e38f, right = -3.4e38f;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float y0 = ys[i], y1 = ys[j];
        if ((y < y0) == (y < y1))
            continue;
        float at = xs[i] + (xs[j] - xs[i]) * (y - y0) / (y1 - y0);
        left = at < left ? at : left;
        right = at > right ? at : right;
    }
    if (!((LONG)left <= px && px < (LONG)right))
        return FALSE;
    float w1 = ((by - cy) * (x - c->sx) + (c->sx - b->sx) * (y - cy)) / ((by - cy) * (a->sx - c->sx) + (c->sx - b->sx) * (ay - cy));
    float w2 = ((cy - ay) * (x - c->sx) + (a->sx - c->sx) * (y - cy)) / ((by - cy) * (a->sx - c->sx) + (c->sx - b->sx) * (ay - cy));
    *z = w1 * a->sz + w2 * b->sz + (1.0f - w1 - w2) * c->sz;
    return TRUE;
}

static HRESULT add_pick(RUN *run, const BYTE *record, float z)
{
    D3DLEGACY *l = run->dev->legacy;
    D3DPICKRECORD *picks = l->picks ? HeapReAlloc(GetProcessHeap(), 0, l->picks, (l->pick_count + 1) * sizeof(*picks))
                                    : HeapAlloc(GetProcessHeap(), 0, sizeof(*picks));
    if (!picks)
        return DDERR_OUTOFMEMORY;
    l->picks = picks;
    picks[l->pick_count++] = (D3DPICKRECORD){D3DOP_TRIANGLE, 0, (DWORD)(record - run->start), z};
    return D3D_OK;
}

/* ------------------------------------------------------------------ primitives */

static BOOL in_range(RUN *run, DWORD i)
{
    return i < run->count;
}

static HRESULT triangles(RUN *run, const D3DTRIANGLE *t, DWORD count)
{
    D3DDEV *dev = run->dev;
    D3DLEGACY *l = dev->legacy;
    if (!run->pick && run->clipped) {
        l->stats.dwTrianglesDrawn += count;
        if (dev->rs[D3DRENDERSTATE_ZVISIBLE]) {
            l->status &= ~D3DSTATUS_ZNOTVISIBLE;
            return D3D_OK;
        }
    }
    for (DWORD i = 0; i < count; i++) {
        DWORD v[16] = {t[i].v1, t[i].v2, t[i].v3}, n = 3;
        if (!in_range(run, v[0]) || !in_range(run, v[1]) || !in_range(run, v[2]))
            continue;
        WORD a = l->tl_codes[v[0]], b = l->tl_codes[v[1]], c = l->tl_codes[v[2]];
        if (run->clipped) {
            if (a & b & c)
                continue;
            if ((a | b | c) & 0x3F)
                n = clip_polygon(run, v, 3, (a | b | c) & 0x3F);
        }
        for (DWORD k = 1; k + 1 < n; k++) {
            if (run->pick) {
                float z;
                if (pick_test(tl_at(l, v[0]), tl_at(l, v[k]), tl_at(l, v[k + 1]), run->pick->x1, run->pick->y1, &z)) {
                    HRESULT hr = add_pick(run, (const BYTE *)&t[i], z);
                    if (FAILED(hr))
                        return hr;
                }
                continue;
            }
            if (!add_index(run, v[0]) || !add_index(run, v[k]) || !add_index(run, v[k + 1]))
                return DDERR_OUTOFMEMORY;
        }
        /* the indices stay within a word. */
        if (run->count + run->extra > 0xFFF0) {
            HRESULT hr = flush(run, D3DPT_TRIANGLELIST);
            if (FAILED(hr))
                return hr;
        }
    }
    if (run->pick) {
        run->extra = 0;
        return D3D_OK;
    }
    return flush(run, D3DPT_TRIANGLELIST);
}

static HRESULT lines(RUN *run, const D3DLINE *line, DWORD count)
{
    D3DDEV *dev = run->dev;
    D3DLEGACY *l = dev->legacy;
    if (run->clipped) {
        l->stats.dwLinesDrawn += count;
        if (dev->rs[D3DRENDERSTATE_ZVISIBLE]) {
            l->status &= ~D3DSTATUS_ZNOTVISIBLE;
            return D3D_OK;
        }
    }
    for (DWORD i = 0; i < count; i++) {
        DWORD a = line[i].v1, b = line[i].v2;
        if (!in_range(run, a) || !in_range(run, b))
            continue;
        WORD ca = l->tl_codes[a], cb = l->tl_codes[b];
        if (run->clipped && (ca & cb))
            continue;
        if (run->clipped && ((ca | cb) & 0x3F)) {
            /* the segment's part inside every plane. */
            float t0 = 0.0f, t1 = 1.0f;
            for (int plane = 0; plane < 6; plane++) {
                float da = plane_distance(l->tl_clip + (SIZE_T)a * 4, plane);
                float db = plane_distance(l->tl_clip + (SIZE_T)b * 4, plane);
                if (da < 0.0f && db < 0.0f)
                    t1 = -1.0f;
                else if (da < 0.0f)
                    t0 = da / (da - db) > t0 ? da / (da - db) : t0;
                else if (db < 0.0f)
                    t1 = da / (da - db) < t1 ? da / (da - db) : t1;
            }
            if (t0 >= t1)
                continue;
            DWORD na = t0 > 0.0f ? lerp_vertex(run, a, b, t0) : a;
            DWORD nb = t1 < 1.0f ? lerp_vertex(run, a, b, t1) : b;
            a = na;
            b = nb;
        }
        if (!add_index(run, a) || !add_index(run, b))
            return DDERR_OUTOFMEMORY;
    }
    return flush(run, D3DPT_LINELIST);
}

static HRESULT points(RUN *run, const D3DPOINT *p, DWORD count)
{
    D3DDEV *dev = run->dev;
    D3DLEGACY *l = dev->legacy;
    if (run->clipped) {
        for (DWORD i = 0; i < count; i++)
            l->stats.dwPointsDrawn += p[i].wCount;
        if (dev->rs[D3DRENDERSTATE_ZVISIBLE]) {
            l->status &= ~D3DSTATUS_ZNOTVISIBLE;
            return D3D_OK;
        }
    }
    for (DWORD i = 0; i < count; i++)
        for (DWORD v = p[i].wFirst; v < (DWORD)p[i].wFirst + p[i].wCount && in_range(run, v); v++)
            if ((!run->clipped || !l->tl_codes[v]) && !add_index(run, v))
                return DDERR_OUTOFMEMORY;
    return flush(run, D3DPT_POINTLIST);
}

/* ------------------------------------------------------------------ states and matrices */

static HRESULT state_transform(RUN *run, const D3DSTATE *s, DWORD count)
{
    D3DLEGACY *l = run->dev->legacy;
    for (DWORD i = 0; i < count; i++) {
        DWORD type = s[i].dtstTransformStateType, value = s[i].dwArg[0];
        if (type > D3DSTATE_OVERRIDE_BIAS) {
            DWORD bit = 1u << ((type - D3DSTATE_OVERRIDE_BIAS) & 31);
            l->transform_locks = value ? l->transform_locks | bit : l->transform_locks & ~bit;
            continue;
        }
        if (type < 32 && (l->transform_locks >> type & 1))
            continue;
        /* a null handle ends the instruction, without an error. */
        if (!value)
            return D3D_OK;
        if (type < D3DTRANSFORMSTATE_WORLD || type > D3DTRANSFORMSTATE_PROJECTION)
            return E_FAIL;
        D3DMATRIXOBJ *m = D3dMatrixHandle(run->dev, value);
        if (!m)
            return DDERR_INVALIDPARAMS;
        l->bound[type] = value;
        D3dSetTransform(run->dev, type, &m->m);
    }
    return D3D_OK;
}

static HRESULT state_render(RUN *run, const D3DSTATE *s, DWORD count)
{
    D3DLEGACY *l = run->dev->legacy;
    for (DWORD i = 0; i < count; i++) {
        DWORD type = s[i].drstRenderStateType, value = s[i].dwArg[0];
        if (type > D3DSTATE_OVERRIDE_BIAS) {
            DWORD t = (type - D3DSTATE_OVERRIDE_BIAS) & 255, bit = 1u << (t & 31);
            l->rs_locks[t >> 5] = value ? l->rs_locks[t >> 5] | bit : l->rs_locks[t >> 5] & ~bit;
            continue;
        }
        if (!type || type > 255 || (l->rs_locks[type >> 5] >> (type & 31) & 1))
            continue;
        D3dLegacySetRenderState(run->dev, type, value);
    }
    return D3D_OK;
}

static HRESULT state_light(RUN *run, const D3DSTATE *s, DWORD count)
{
    for (DWORD i = 0; i < count; i++) {
        HRESULT hr = D3dLegacySetLightState(run->dev, s[i].dlstLightStateType, s[i].dwArg[0]);
        if (FAILED(hr))
            return hr;
    }
    return D3D_OK;
}

static HRESULT matrix_load(RUN *run, const D3DMATRIXLOAD *m, DWORD count)
{
    for (DWORD i = 0; i < count; i++) {
        D3DMATRIXOBJ *dst = D3dMatrixHandle(run->dev, m[i].hDestMatrix);
        D3DMATRIXOBJ *src = D3dMatrixHandle(run->dev, m[i].hSrcMatrix);
        if (!dst || !src)
            return DDERR_INVALIDPARAMS;
        dst->m = src->m;
        D3dMatrixChanged(run->dev, m[i].hDestMatrix);
    }
    return D3D_OK;
}

static HRESULT matrix_multiply(RUN *run, const D3DMATRIXMULTIPLY *m, DWORD count)
{
    for (DWORD i = 0; i < count; i++) {
        D3DMATRIXOBJ *a = D3dMatrixHandle(run->dev, m[i].hSrcMatrix1);
        D3DMATRIXOBJ *b = D3dMatrixHandle(run->dev, m[i].hSrcMatrix2);
        D3DMATRIXOBJ *dst = D3dMatrixHandle(run->dev, m[i].hDestMatrix);
        if (!a || !b || !dst)
            return E_FAIL;
        dst->m = D3dMultiply(&a->m, &b->m);
        D3dMatrixChanged(run->dev, m[i].hDestMatrix);
    }
    return D3D_OK;
}

static HRESULT texture_load(RUN *run, const D3DTEXTURELOAD *t, DWORD count)
{
    for (DWORD i = 0; i < count; i++) {
        D3DTEXHANDLE *dst = D3dTextureHandle(run->dev, t[i].hDestTexture);
        D3DTEXHANDLE *src = D3dTextureHandle(run->dev, t[i].hSrcTexture);
        if (!dst || !src)
            return D3DERR_TEXTURE_LOAD_FAILED;
        HRESULT hr = D3dTextureLoad(dst->texture->surface, src->texture->surface);
        if (hr == DDERR_NOTFOUND || hr == DDERR_NOPALETTEATTACHED)
            return D3DERR_TEXTURE_LOAD_FAILED;
        if (FAILED(hr))
            return hr;
    }
    return D3D_OK;
}

/* ------------------------------------------------------------------ the interpreter */

static void write_status(D3DDEV *dev, D3DSTATUS *out)
{
    D3DLEGACY *l = dev->legacy;
    const D3DVIEWPORT7 *vp = &dev->viewport;
    LONG x1 = (LONG)l->extents[0] - 1, y1 = (LONG)l->extents[1] - 1, x2 = (LONG)l->extents[2] + 1,
         y2 = (LONG)l->extents[3] + 1;
    LONG vx = (LONG)vp->dwX, vy = (LONG)vp->dwY, vr = vx + (LONG)vp->dwWidth, vb = vy + (LONG)vp->dwHeight;
    out->dwFlags = 0;
    out->dwStatus = l->status;
    out->drExtent.x1 = x1 < vx ? vx : x1;
    out->drExtent.y1 = y1 < vy ? vy : y1;
    out->drExtent.x2 = x2 > vr ? vr : x2;
    out->drExtent.y2 = y2 > vb ? vb : y2;
}

/* the stream from its first instruction to EXIT, its end or an opcode it does not know. */
static HRESULT interpret(RUN *run, D3DSTATUS *status)
{
    D3DDEV *dev = run->dev;
    D3DLEGACY *l = dev->legacy;
    const D3DEXECUTEDATA *data = &run->b->data;
    const BYTE *memory = run->b->memory, *end = memory + run->b->size;
    if (data->dwInstructionOffset >= run->b->size)
        return D3D_OK;
    const BYTE *ip = memory + data->dwInstructionOffset;
    if (data->dwInstructionLength && data->dwInstructionLength < (DWORD)(end - ip))
        end = ip + data->dwInstructionLength;
    run->start = ip;
    /* as many vertices as the device takes at least, as windows sizes its buffer. */
    DWORD max = dev->hal ? l->hw.dwMaxVertexCount : l->sw.dwMaxVertexCount;
    run->count = data->dwVertexCount > max ? data->dwVertexCount : max;
    if (run->count > 0xFFF0)
        run->count = 0xFFF0;
    if (!reserve(l, run->count ? run->count : 1))
        return DDERR_OUTOFMEMORY;
    for (DWORD steps = 0; steps < 0x1000000; steps++) {
        if (ip + sizeof(D3DINSTRUCTION) > end)
            break;
        D3DINSTRUCTION in;
        memcpy(&in, ip, sizeof(in));
        const BYTE *records = ip + sizeof(in);
        static const BYTE SIZES[15] = {0, 4, 4, 8, 8, 12, 8, 8, 8, 16, 8, 0, 16, 0, 24};
        DWORD size = in.bOpcode < RTL_NUMBER_OF(SIZES) ? SIZES[in.bOpcode] : 0;
        /* the pick pass trusts bSize for what it skips. */
        BOOL skipped = run->pick && (in.bOpcode == D3DOP_POINT || in.bOpcode == D3DOP_LINE ||
                                     in.bOpcode == D3DOP_STATELIGHT || in.bOpcode == D3DOP_TEXTURELOAD);
        if (in.bOpcode == D3DOP_SPAN || skipped || (run->pick && (!in.bOpcode || in.bOpcode > D3DOP_SETSTATUS))) {
            ip = records + (SIZE_T)in.bSize * in.wCount;
            continue;
        }
        if (!in.bOpcode || in.bOpcode > D3DOP_SETSTATUS) {
            memset(status, 0, sizeof(*status));
            return D3D_OK;
        }
        if (in.bOpcode == D3DOP_EXIT)
            break;
        DWORD count = in.wCount;
        if (size && count > (DWORD)(end - records) / size)
            count = (DWORD)(end - records) / size;
        HRESULT hr = D3D_OK;
        switch (in.bOpcode) {
        case D3DOP_POINT:
            hr = points(run, (const D3DPOINT *)records, count);
            break;
        case D3DOP_LINE:
            hr = lines(run, (const D3DLINE *)records, count);
            break;
        case D3DOP_TRIANGLE:
            if (!run->pick || !dev->rs[D3DRENDERSTATE_ZVISIBLE])
                hr = triangles(run, (const D3DTRIANGLE *)records, count);
            break;
        case D3DOP_MATRIXLOAD:
            hr = matrix_load(run, (const D3DMATRIXLOAD *)records, count);
            break;
        case D3DOP_MATRIXMULTIPLY:
            hr = matrix_multiply(run, (const D3DMATRIXMULTIPLY *)records, count);
            break;
        case D3DOP_STATETRANSFORM:
            hr = state_transform(run, (const D3DSTATE *)records, count);
            break;
        case D3DOP_STATELIGHT:
            hr = state_light(run, (const D3DSTATE *)records, count);
            break;
        case D3DOP_STATERENDER:
            hr = state_render(run, (const D3DSTATE *)records, count);
            break;
        case D3DOP_PROCESSVERTICES:
            for (DWORD i = 0; i < count && SUCCEEDED(hr); i++)
                hr = process_vertices(run, (const D3DPROCESSVERTICES *)records + i);
            break;
        case D3DOP_TEXTURELOAD:
            hr = texture_load(run, (const D3DTEXTURELOAD *)records, count);
            break;
        case D3DOP_BRANCHFORWARD:
            if (count) {
                const D3DBRANCH *b = (const D3DBRANCH *)records;
                BOOL taken = ((l->status & b->dwMask) == b->dwValue) != (b->bNegate != 0);
                if (taken && !b->dwOffset) {
                    write_status(dev, status);
                    return D3D_OK;
                }
                if (taken) {
                    ip += b->dwOffset;
                    if (ip < memory || ip >= end)
                        goto done;
                    continue;
                }
            }
            break;
        case D3DOP_SETSTATUS:
            if (count) {
                const D3DSTATUS *s = (const D3DSTATUS *)records;
                if (s->dwFlags & D3DSETSTATUS_STATUS)
                    l->status = s->dwStatus;
                if (s->dwFlags & D3DSETSTATUS_EXTENTS) {
                    l->extents[0] = (float)s->drExtent.x1;
                    l->extents[1] = (float)s->drExtent.y1;
                    l->extents[2] = (float)s->drExtent.x2;
                    l->extents[3] = (float)s->drExtent.y2;
                }
            }
            break;
        }
        if (FAILED(hr))
            return hr;
        ip = records + (SIZE_T)size * in.wCount;
    }
done:
    write_status(dev, status);
    return D3D_OK;
}

static HRESULT run_stream(D3DDEV *dev, D3DEXECBUF *b, BOOL clipped, const D3DRECT *pick, D3DSTATUS *status)
{
    RUN run = {dev, b, clipped, pick};
    HRESULT hr = interpret(&run, status);
    HeapFree(GetProcessHeap(), 0, run.indices);
    return hr;
}

HRESULT D3dExecute(D3DDEV *dev, D3DEXECBUF *b, DWORD flags)
{
    if (dev->target->lost || (dev->depth && dev->depth->lost))
        return DDERR_SURFACELOST;
    if (flags != D3DEXECUTE_CLIPPED && flags != D3DEXECUTE_UNCLIPPED)
        return DDERR_INVALIDPARAMS;
    D3DSTATUS status;
    HRESULT hr = run_stream(dev, b, flags == D3DEXECUTE_CLIPPED, nullptr, &status);
    if (SUCCEEDED(hr))
        b->data.dsStatus = status;
    return hr;
}

/* the triangles under a pixel, whatever happens to the pass; the status it makes is dropped. */
HRESULT D3dPick(D3DDEV *dev, D3DEXECBUF *b, const D3DRECT *rect)
{
    D3DLEGACY *l = dev->legacy;
    l->pick_count = 0;
    D3DSTATUS status;
    run_stream(dev, b, TRUE, rect, &status);
    return D3D_OK;
}
