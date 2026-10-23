/*
 * IDirect3DVertexShader9 and IDirect3DPixelShader9: the bytecode is read
 * once, then translated to a d3d11 shader per state key on first draw.
 */
#include "d3d9_private.h"

static D9Shader *impl(void *iface) { return CONTAINING_RECORD((IDirect3DVertexShader9 *)iface, D9Shader, iface.vs); }

D9Shader *D9ShaderFrom(void *iface) { return iface ? impl(iface) : nullptr; }

static void destroy(D9Shader *s)
{
    for (D9Variant *v = s->variants, *next; v; v = next) {
        next = v->next;
        if (v->shader)
            v->shader->lpVtbl->Release(v->shader);
        if (v->stream_out)
            v->stream_out->lpVtbl->Release(v->stream_out);
        HeapFree(GetProcessHeap(), 0, v->dxbc);
        HeapFree(GetProcessHeap(), 0, v);
    }
    D9FreeShaderInfo(&s->info);
    D9ResourceClear(&s->base);
    HeapFree(GetProcessHeap(), 0, s);
}

static void destroy_resource(D9Resource *r) { destroy(CONTAINING_RECORD(r, D9Shader, base)); }

D9Variant *D9ShaderVariant(D9Shader *s, const D9ShaderKey *key)
{
    if (s->last && !memcmp(&s->last->key, key, sizeof(*key)))
        return s->last;
    for (D9Variant *v = s->variants; v; v = v->next)
        if (!memcmp(&v->key, key, sizeof(*key)))
            return s->last = v;
    D9Variant *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v)
        return nullptr;
    v->key = *key;
    v->info = &s->info;
    D9Device *device = s->base.device;
    HRESULT hr = D9TranslateShader(&s->info, key, &v->dxbc, &v->size, &v->outputs);
    if (SUCCEEDED(hr)) {
        ID3D11Device1 *d3d11 = device->d3d11;
        if (s->info.pixel)
            hr = d3d11->lpVtbl->CreatePixelShader(d3d11, v->dxbc, v->size, nullptr, (ID3D11PixelShader **)&v->shader);
        else
            hr = d3d11->lpVtbl->CreateVertexShader(d3d11, v->dxbc, v->size, nullptr, (ID3D11VertexShader **)&v->shader);
        if (FAILED(hr))
            D9Log("d3d9: d3d11 refused the translation of a %s shader: %08x", s->info.pixel ? "pixel" : "vertex", hr);
    }
    /* failed translations are kept, so draws that need them skip without retrying. */
    if (FAILED(hr))
        v->shader = nullptr;
    if (s->info.pixel) {
        HeapFree(GetProcessHeap(), 0, v->dxbc);
        v->dxbc = nullptr;
    }
    v->next = s->variants;
    s->variants = v;
    return s->last = v;
}

static HRESULT STDMETHODCALLTYPE shader_query(IDirect3DVertexShader9 *iface, REFIID riid, void **out)
{
    D9Shader *s = impl(iface);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, s->info.pixel ? &IID_IDirect3DPixelShader9 : &IID_IDirect3DVertexShader9)) {
        *out = iface;
        D9ResourceAddRef(&s->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE shader_add_ref(IDirect3DVertexShader9 *iface) { return D9ResourceAddRef(&impl(iface)->base); }

static ULONG STDMETHODCALLTYPE shader_release(IDirect3DVertexShader9 *iface)
{
    return D9ResourceRelease(&impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE shader_get_device(IDirect3DVertexShader9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE shader_get_function(IDirect3DVertexShader9 *iface, void *data, UINT *size)
{
    D9Shader *s = impl(iface);
    if (!size)
        return D3DERR_INVALIDCALL;
    UINT bytes = s->info.words * 4;
    if (!data) {
        *size = bytes;
        return D3D_OK;
    }
    if (*size < bytes)
        return D3DERR_INVALIDCALL;
    memcpy(data, s->info.code, bytes);
    *size = bytes;
    return D3D_OK;
}

static const IDirect3DVertexShader9Vtbl shader_vtbl = {
    shader_query, shader_add_ref, shader_release, shader_get_device, shader_get_function,
};

HRESULT D9CreateShader(D9Device *device, const DWORD *code, BOOL pixel, D9Shader **out)
{
    D9Shader *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    HRESULT hr = D9ParseShader(code, pixel, &s->info);
    if (FAILED(hr)) {
        D9FreeShaderInfo(&s->info);
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    s->iface.vs.lpVtbl = &shader_vtbl;
    D9ResourceInit(&s->base, device, 0, D3DPOOL_DEFAULT, 0, D3DFMT_UNKNOWN, destroy_resource);
    *out = s;
    return D3D_OK;
}

/* ------------------------------------------------------------------ vertex declarations */

static D9VertexDecl *decl_impl(IDirect3DVertexDeclaration9 *iface)
{
    return CONTAINING_RECORD(iface, D9VertexDecl, iface);
}

D9VertexDecl *D9VertexDeclFrom(IDirect3DVertexDeclaration9 *iface) { return iface ? decl_impl(iface) : nullptr; }

static void destroy_decl(D9VertexDecl *d)
{
    for (D9Layout *l = d->layouts, *next; l; l = next) {
        next = l->next;
        if (l->layout)
            l->layout->lpVtbl->Release(l->layout);
        HeapFree(GetProcessHeap(), 0, l);
    }
    D9ResourceClear(&d->base);
    HeapFree(GetProcessHeap(), 0, d);
}

static void destroy_decl_resource(D9Resource *r) { destroy_decl(CONTAINING_RECORD(r, D9VertexDecl, base)); }

static HRESULT STDMETHODCALLTYPE decl_query(IDirect3DVertexDeclaration9 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DVertexDeclaration9)) {
        *out = iface;
        D9ResourceAddRef(&decl_impl(iface)->base);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE decl_add_ref(IDirect3DVertexDeclaration9 *iface)
{
    return D9ResourceAddRef(&decl_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE decl_release(IDirect3DVertexDeclaration9 *iface)
{
    return D9ResourceRelease(&decl_impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE decl_get_device(IDirect3DVertexDeclaration9 *iface, IDirect3DDevice9 **device)
{
    return D9ResourceGetDevice(&decl_impl(iface)->base, device);
}

static HRESULT STDMETHODCALLTYPE decl_get_declaration(IDirect3DVertexDeclaration9 *iface, D3DVERTEXELEMENT9 *elements,
                                                      UINT *count)
{
    D9VertexDecl *d = decl_impl(iface);
    if (!count)
        return D3DERR_INVALIDCALL;
    /* the count includes the end marker, which is copied too. */
    if (elements) {
        memcpy(elements, d->elements, d->count * sizeof(*elements));
        elements[d->count] = (D3DVERTEXELEMENT9)D3DDECL_END();
    }
    *count = d->count + 1;
    return D3D_OK;
}

static const IDirect3DVertexDeclaration9Vtbl decl_vtbl = {
    decl_query, decl_add_ref, decl_release, decl_get_device, decl_get_declaration,
};

/* the fvf matching a declaration, or 0. */
static DWORD fvf_of(const D3DVERTEXELEMENT9 *e, UINT count)
{
    DWORD fvf = 0;
    UINT offset = 0, i = 0, texcoords = 0;
    if (i < count && e[i].Usage == D3DDECLUSAGE_POSITION && e[i].Type == D3DDECLTYPE_FLOAT3 && !e[i].Offset)
        fvf |= D3DFVF_XYZ, offset = 12, i++;
    else if (i < count && e[i].Usage == D3DDECLUSAGE_POSITIONT && e[i].Type == D3DDECLTYPE_FLOAT4 && !e[i].Offset)
        fvf |= D3DFVF_XYZRHW, offset = 16, i++;
    else
        return 0;
    if (i < count && e[i].Usage == D3DDECLUSAGE_NORMAL && e[i].Type == D3DDECLTYPE_FLOAT3 && e[i].Offset == offset)
        fvf |= D3DFVF_NORMAL, offset += 12, i++;
    if (i < count && e[i].Usage == D3DDECLUSAGE_PSIZE && e[i].Type == D3DDECLTYPE_FLOAT1 && e[i].Offset == offset)
        fvf |= D3DFVF_PSIZE, offset += 4, i++;
    for (UINT c = 0; c < 2; c++)
        if (i < count && e[i].Usage == D3DDECLUSAGE_COLOR && e[i].UsageIndex == c &&
            e[i].Type == D3DDECLTYPE_D3DCOLOR && e[i].Offset == offset)
            fvf |= c ? D3DFVF_SPECULAR : D3DFVF_DIFFUSE, offset += 4, i++;
    for (; i < count && texcoords < 8; i++, texcoords++) {
        if (e[i].Usage != D3DDECLUSAGE_TEXCOORD || e[i].UsageIndex != texcoords || e[i].Offset != offset ||
            e[i].Type > D3DDECLTYPE_FLOAT4)
            return 0;
        static const BYTE FORMAT[] = {D3DFVF_TEXTUREFORMAT1, D3DFVF_TEXTUREFORMAT2, D3DFVF_TEXTUREFORMAT3,
                                      D3DFVF_TEXTUREFORMAT4};
        fvf |= (DWORD)FORMAT[e[i].Type] << (texcoords * 2 + 16);
        offset += 4 * (e[i].Type + 1);
    }
    if (i != count)
        return 0;
    return fvf | texcoords << D3DFVF_TEXCOUNT_SHIFT;
}

HRESULT D9CreateVertexDecl(D9Device *device, const D3DVERTEXELEMENT9 *elements, DWORD fvf, D9VertexDecl **out)
{
    UINT count = 0;
    while (count <= MAXD3DDECLLENGTH && elements[count].Stream != 0xFF)
        count++;
    if (count > MAXD3DDECLLENGTH)
        return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < count; i++)
        if (elements[i].Type >= D3DDECLTYPE_UNUSED || elements[i].Stream >= D9_MAX_STREAMS ||
            elements[i].Usage > MAXD3DDECLUSAGE)
            return D3DERR_INVALIDCALL;
    D9VertexDecl *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->iface.lpVtbl = &decl_vtbl;
    D9ResourceInit(&d->base, device, 0, D3DPOOL_DEFAULT, 0, D3DFMT_UNKNOWN, destroy_decl_resource);
    memcpy(d->elements, elements, count * sizeof(*elements));
    d->count = count;
    d->fvf = fvf ? fvf : fvf_of(elements, count);
    for (UINT i = 0; i < count; i++) {
        d->streams |= 1u << elements[i].Stream;
        if (elements[i].Usage == D3DDECLUSAGE_POSITIONT)
            d->pretransformed = TRUE;
        if (elements[i].Usage == D3DDECLUSAGE_COLOR && elements[i].UsageIndex < 2)
            d->colors |= 1u << elements[i].UsageIndex;
    }
    *out = d;
    return D3D_OK;
}

/* elements in fvf order: position, blend weights, normal, size, colors, texture coordinates. */
UINT D9FvfElements(DWORD fvf, D3DVERTEXELEMENT9 *e)
{
    UINT n = 0;
    WORD offset = 0;
#define ADD(type, usage, index, size)                                                                                  \
    do {                                                                                                               \
        e[n++] = (D3DVERTEXELEMENT9){0, offset, type, D3DDECLMETHOD_DEFAULT, usage, index};                          \
        offset += size;                                                                                                \
    } while (0)
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    if (position == D3DFVF_XYZRHW) {
        ADD(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, 16);
    } else if (position == D3DFVF_XYZW) {
        ADD(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0, 16);
    } else if (position) {
        ADD(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12);
        UINT betas = position >= D3DFVF_XYZB1 ? (position - D3DFVF_XYZB1) / 2 + 1 : 0;
        BOOL indices = fvf & (D3DFVF_LASTBETA_UBYTE4 | D3DFVF_LASTBETA_D3DCOLOR);
        UINT weights = betas - (indices ? 1 : 0);
        if (weights)
            ADD(D3DDECLTYPE_FLOAT1 + weights - 1, D3DDECLUSAGE_BLENDWEIGHT, 0, 4 * weights);
        if (indices)
            ADD(fvf & D3DFVF_LASTBETA_UBYTE4 ? D3DDECLTYPE_UBYTE4 : D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_BLENDINDICES,
                0, 4);
    }
    if (fvf & D3DFVF_NORMAL)
        ADD(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0, 12);
    if (fvf & D3DFVF_PSIZE)
        ADD(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, 4);
    if (fvf & D3DFVF_DIFFUSE)
        ADD(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
    if (fvf & D3DFVF_SPECULAR)
        ADD(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, 4);
    UINT texcoords = D9_MIN((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT, 8);
    for (UINT i = 0; i < texcoords; i++) {
        static const BYTE TYPE[] = {D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1};
        static const BYTE SIZE[] = {8, 12, 16, 4};
        UINT format = (fvf >> (i * 2 + 16)) & 3;
        ADD(TYPE[format], D3DDECLUSAGE_TEXCOORD, i, SIZE[format]);
    }
#undef ADD
    e[n] = (D3DVERTEXELEMENT9)D3DDECL_END();
    return n;
}

/* also says whether shaders read the type as integers converted to floats. */
static DXGI_FORMAT element_format(BYTE type, BYTE *integer)
{
    *integer = 0;
    switch (type) {
    case D3DDECLTYPE_FLOAT1:
        return DXGI_FORMAT_R32_FLOAT;
    case D3DDECLTYPE_FLOAT2:
        return DXGI_FORMAT_R32G32_FLOAT;
    case D3DDECLTYPE_FLOAT3:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case D3DDECLTYPE_FLOAT4:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case D3DDECLTYPE_D3DCOLOR:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:
        *integer = 1;
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case D3DDECLTYPE_SHORT2:
        *integer = 2;
        return DXGI_FORMAT_R16G16_SINT;
    case D3DDECLTYPE_SHORT4:
        *integer = 2;
        return DXGI_FORMAT_R16G16B16A16_SINT;
    case D3DDECLTYPE_UBYTE4N:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2N:
        return DXGI_FORMAT_R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:
        return DXGI_FORMAT_R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:
        return DXGI_FORMAT_R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    case D3DDECLTYPE_UDEC3:
        *integer = 1;
        return DXGI_FORMAT_R10G10B10A2_UINT;
    case D3DDECLTYPE_DEC3N:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DDECLTYPE_FLOAT16_2:
        return DXGI_FORMAT_R16G16_FLOAT;
    case D3DDECLTYPE_FLOAT16_4:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

/* 2 bits per register: 1 unsigned, 2 signed. */
UINT D9DeclIntegerInputs(const D9VertexDecl *d, const D9ShaderInfo *vs)
{
    UINT bits = 0;
    for (UINT r = 0; r < 16; r++) {
        if (!(vs->in_declared & (1u << r)))
            continue;
        for (UINT i = 0; i < d->count; i++)
            if (d->elements[i].Usage == vs->in_usage[r] && d->elements[i].UsageIndex == vs->in_index[r]) {
                BYTE integer;
                element_format(d->elements[i].Type, &integer);
                bits |= (UINT)integer << (r * 2);
                break;
            }
    }
    return bits;
}

ID3D11InputLayout *D9DeclLayout(D9VertexDecl *d, D9Device *device, D9Variant *vs, UINT instanced, const UINT *rates)
{
    for (D9Layout *l = d->layouts; l; l = l->next)
        if (l->variant == vs && l->instanced == instanced && !memcmp(l->rates, rates, sizeof(l->rates)))
            return l->layout;
    D3D11_INPUT_ELEMENT_DESC descs[MAXD3DDECLLENGTH + 16];
    UINT n = 0;
    for (UINT i = 0; i < d->count; i++) {
        const D3DVERTEXELEMENT9 *e = &d->elements[i];
        BYTE integer;
        DXGI_FORMAT format = element_format(e->Type, &integer);
        BOOL inst = instanced & (1u << e->Stream);
        descs[n++] = (D3D11_INPUT_ELEMENT_DESC){D9UsageName(e->Usage), e->UsageIndex, format, e->Stream, e->Offset,
                                                inst ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA,
                                                inst ? rates[e->Stream] : 0};
    }
    /* inputs missing from the declaration read zeros from the last stream, which binds an empty buffer. */
    const D9ShaderInfo *info = vs->info;
    for (UINT r = 0; r < 16; r++) {
        if (!(info->in_declared & (1u << r)))
            continue;
        BOOL found = FALSE;
        for (UINT i = 0; i < d->count && !found; i++)
            found = d->elements[i].Usage == info->in_usage[r] && d->elements[i].UsageIndex == info->in_index[r];
        if (!found)
            descs[n++] = (D3D11_INPUT_ELEMENT_DESC){D9UsageName(info->in_usage[r]), info->in_index[r],
                                                    DXGI_FORMAT_R32G32B32A32_FLOAT, D9_ZERO_STREAM, 0,
                                                    D3D11_INPUT_PER_VERTEX_DATA, 0};
    }
    D9Layout *l = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*l));
    if (!l)
        return nullptr;
    l->variant = vs;
    l->instanced = instanced;
    memcpy(l->rates, rates, sizeof(l->rates));
    HRESULT hr = device->d3d11->lpVtbl->CreateInputLayout(device->d3d11, descs, n, vs->dxbc, vs->size, &l->layout);
    if (FAILED(hr)) {
        D9Log("d3d9: no input layout of a declaration of %u elements: %08x", d->count, hr);
        l->layout = nullptr;
    }
    l->next = d->layouts;
    d->layouts = l;
    return l->layout;
}
