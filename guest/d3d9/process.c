/*
 * ProcessVertices: windows runs the vertex pipeline on the cpu. here the
 * gpu runs it like a draw and streams the outputs out, read back once
 * done and written as screen positions plus what the destination
 * declaration asks for.
 */
#include "d3d9_private.h"

/* streams out the position, then each output register written. */
static ID3D11GeometryShader *stream_out(D9Device *d, D9Variant *v, UINT *stride)
{
    *stride = 16 * (1 + __builtin_popcount(v->outputs & ((1u << D9_SLOT_POSITION) - 1)));
    if (v->stream_out)
        return v->stream_out;
    D3D11_SO_DECLARATION_ENTRY entries[1 + D9_SLOT_POSITION];
    UINT count = 0;
    entries[count++] = (D3D11_SO_DECLARATION_ENTRY){0, "SV_Position", 0, 0, 4, 0};
    for (UINT slot = 0; slot < D9_SLOT_POSITION; slot++) {
        if (!(v->outputs & (1u << slot)))
            continue;
        const char *name;
        UINT index;
        D9SlotSemantic(slot, &name, &index);
        entries[count++] = (D3D11_SO_DECLARATION_ENTRY){0, name, index, 0, 4, 0};
    }
    UINT strides[1] = {*stride};
    if (FAILED(d->d3d11->lpVtbl->CreateGeometryShaderWithStreamOutput(d->d3d11, v->dxbc, v->size, entries, count,
                                                                       strides, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                                                                       nullptr, &v->stream_out)))
        v->stream_out = nullptr;
    return v->stream_out;
}

static BOOL processed_buffers(D9Device *d, UINT size)
{
    if (d->processed_size >= size)
        return TRUE;
    if (d->processed)
        d->processed->lpVtbl->Release(d->processed);
    if (d->processed_read)
        d->processed_read->lpVtbl->Release(d->processed_read);
    d->processed = d->processed_read = nullptr;
    d->processed_size = 0;
    size = D9_MAX(D9_ALIGN(size, 65536), 65536);
    D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT};
    D3D11_BUFFER_DESC read = {size, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ};
    if (FAILED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &desc, nullptr, &d->processed)) ||
        FAILED(d->d3d11->lpVtbl->CreateBuffer(d->d3d11, &read, nullptr, &d->processed_read)))
        return FALSE;
    d->processed_size = size;
    return TRUE;
}

static UINT type_size(BYTE type)
{
    return type <= D3DDECLTYPE_FLOAT4 ? 4 * (type + 1u) : type == D3DDECLTYPE_D3DCOLOR ? 4 : 0;
}

static DWORD color_of(const float *c)
{
    DWORD v = 0;
    static const UINT SHIFT[4] = {16, 8, 0, 24};
    for (UINT i = 0; i < 4; i++) {
        float x = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        v |= (DWORD)(x * 255.0f + 0.5f) << SHIFT[i];
    }
    return v;
}

/* D3DCLIPSTATUS9 clip codes of a clip space position. */
static DWORD clip_codes(const float *p)
{
    return (p[0] < -p[3] ? D3DCS_LEFT : 0) | (p[0] > p[3] ? D3DCS_RIGHT : 0) | (p[1] > p[3] ? D3DCS_TOP : 0) |
           (p[1] < -p[3] ? D3DCS_BOTTOM : 0) | (p[2] < 0.0f ? D3DCS_FRONT : 0) | (p[2] > p[3] ? D3DCS_BACK : 0);
}

HRESULT D9ProcessVertices(D9Device *d, UINT source, UINT dest, UINT count, D9Buffer *buffer, D9VertexDecl *decl,
                          DWORD flags)
{
    if (buffer->base.type != D3DRTYPE_VERTEXBUFFER || buffer->base.device != d)
        return D3DERR_INVALIDCALL;
    /* the given declaration, else the buffer's fvf. */
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
    UINT element_count;
    if (decl) {
        element_count = decl->count;
        memcpy(elements, decl->elements, element_count * sizeof(*elements));
    } else if (buffer->fvf) {
        element_count = D9FvfElements(buffer->fvf, elements);
    } else {
        return D3DERR_INVALIDCALL;
    }
    UINT stride = 0;
    for (UINT i = 0; i < element_count; i++) {
        if (elements[i].Stream)
            continue;
        UINT size = type_size(elements[i].Type);
        /* like windows, the output declaration may only hold floats and colors. */
        if (!size && decl)
            return D3DERR_INVALIDCALL;
        stride = D9_MAX(stride, elements[i].Offset + D9_MAX(size, 4u));
    }
    if (!stride || (UINT64)(dest + count) * stride > buffer->size)
        return D3DERR_INVALIDCALL;
    if (!count)
        return D3D_OK;

    if (!D9PrepareDraw(d, D3DPT_POINTLIST))
        return D3DERR_INVALIDCALL;
    D9Variant *vs = d->bound_vs;
    UINT so_stride;
    ID3D11GeometryShader *gs = vs && vs->dxbc ? stream_out(d, vs, &so_stride) : nullptr;
    if (!gs || !processed_buffers(d, count * so_stride))
        return D3DERR_INVALIDCALL;
    ID3D11DeviceContext1 *ctx = d->context;
    UINT offset = 0;
    ctx->lpVtbl->GSSetShader(ctx, gs, nullptr, 0);
    ctx->lpVtbl->SOSetTargets(ctx, 1, &d->processed, &offset);
    ctx->lpVtbl->Draw(ctx, count, source);
    ID3D11Buffer *none = nullptr;
    ctx->lpVtbl->SOSetTargets(ctx, 1, &none, &offset);
    ctx->lpVtbl->GSSetShader(ctx, nullptr, nullptr, 0);
    D3D11_BOX box = {0, 0, 0, count * so_stride, 1, 1};
    ctx->lpVtbl->CopySubresourceRegion(ctx, (ID3D11Resource *)d->processed_read, 0, 0, 0, 0,
                                       (ID3D11Resource *)d->processed, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->lpVtbl->Map(ctx, (ID3D11Resource *)d->processed_read, 0, D3D11_MAP_READ, 0, &mapped)))
        return D3DERR_INVALIDCALL;
    BYTE *out;
    if (FAILED(buffer->iface.vb.lpVtbl->Lock(&buffer->iface.vb, dest * stride, count * stride, (void **)&out, 0))) {
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)d->processed_read, 0);
        return D3DERR_INVALIDCALL;
    }

    /* each register's column in the streamed output. */
    int column[D9_SLOT_POSITION];
    UINT next = 1;
    for (UINT slot = 0; slot < D9_SLOT_POSITION; slot++)
        column[slot] = vs->outputs & (1u << slot) ? (int)next++ : -1;
    /*
     * with D3DPV_DONOTCOPYDATA only the position is written, plus the
     * colors when fixed function lights. which elements windows writes is a
     * guess.
     */
    BOOL only_position = flags & D3DPV_DONOTCOPYDATA;
    BOOL lit = !d->vs && d->render_states[D3DRS_LIGHTING];
    const D3DVIEWPORT9 *vp = &d->viewport;
    float half_x = vp->Width ? 1.0f / vp->Width : 0.0f, half_y = vp->Height ? 1.0f / vp->Height : 0.0f;
    float point_size;
    memcpy(&point_size, &d->render_states[D3DRS_POINTSIZE], 4);
    DWORD clip_union = 0, clip_intersection = ~0u;
    for (UINT n = 0; n < count; n++) {
        const float(*reg)[4] = (const float(*)[4])((const BYTE *)mapped.pData + (SIZE_T)n * so_stride);
        BYTE *vertex = out + (SIZE_T)n * stride;
        /* undo the half pixel offset the translation added. */
        float p[4] = {reg[0][0] - reg[0][3] * half_x, reg[0][1] + reg[0][3] * half_y, reg[0][2], reg[0][3]};
        DWORD codes = clip_codes(p);
        clip_union |= codes;
        clip_intersection &= codes;
        float rhw = p[3] ? 1.0f / p[3] : 0.0f;
        float screen[4] = {(p[0] * rhw + 1.0f) * 0.5f * vp->Width + vp->X, (1.0f - p[1] * rhw) * 0.5f * vp->Height + vp->Y,
                           p[2] * rhw * (vp->MaxZ - vp->MinZ) + vp->MinZ, rhw};
        for (UINT i = 0; i < element_count; i++) {
            const D3DVERTEXELEMENT9 *e = &elements[i];
            UINT size = type_size(e->Type);
            if (e->Stream || !size)
                continue;
            float value[4] = {0, 0, 0, 1};
            BOOL position = e->Usage == D3DDECLUSAGE_POSITIONT || (e->Usage == D3DDECLUSAGE_POSITION && !e->UsageIndex);
            if (position) {
                memcpy(value, screen, sizeof(value));
            } else {
                if (only_position && !(lit && e->Usage == D3DDECLUSAGE_COLOR))
                    continue;
                if (e->Usage == D3DDECLUSAGE_PSIZE) {
                    value[0] = point_size;
                } else {
                    /* fixed function writes only colors, texture coordinates and fog. */
                    BOOL common = e->Usage == D3DDECLUSAGE_COLOR || e->Usage == D3DDECLUSAGE_TEXCOORD ||
                                  e->Usage == D3DDECLUSAGE_FOG;
                    int slot = common || d->vs ? D9VaryingSlot(e->Usage, e->UsageIndex) : -1;
                    if (slot < 0 || slot >= D9_SLOT_POSITION || column[slot] < 0)
                        continue;
                    memcpy(value, reg[column[slot]], sizeof(value));
                    /* fixed function's fog factor is guessed to ride in the specular alpha. */
                    if (e->Usage == D3DDECLUSAGE_COLOR && e->UsageIndex == 1 && !d->vs && column[10] >= 0)
                        value[3] = reg[column[10]][0];
                }
            }
            if (e->Type == D3DDECLTYPE_D3DCOLOR) {
                DWORD c = color_of(value);
                memcpy(vertex + e->Offset, &c, 4);
            } else {
                memcpy(vertex + e->Offset, value, size);
            }
        }
    }
    buffer->iface.vb.lpVtbl->Unlock(&buffer->iface.vb);
    ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)d->processed_read, 0);
    d->clip_status.ClipUnion |= clip_union;
    d->clip_status.ClipIntersection &= clip_intersection;
    return D3D_OK;
}
