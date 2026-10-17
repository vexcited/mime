/*
 * vertex input and drawing on a direct3d 8 device: shaders by handle,
 * their declarations made direct3d 9 vertex declarations, vertex shader
 * code given the dcl instructions direct3d 9 needs, stream sources and
 * indices with their base vertex, and the draws.
 */
#include "d3d8_private.h"

static D8Device *impl(IDirect3DDevice8 *iface) { return D8DeviceFrom(iface); }

/* ------------------------------------------------------------------ handle tables */

UINT D8TableAdd(D8Table *t, void *item)
{
    if (!t->count)
        t->count = 1;
    if (t->free_count) {
        UINT index = t->free[--t->free_count];
        t->slots[index] = item;
        return index;
    }
    if (t->count >= t->capacity) {
        UINT capacity = t->capacity ? t->capacity * 2 : 8;
        void **slots = t->slots ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, t->slots, capacity * sizeof(*slots))
                                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(*slots));
        if (!slots)
            return 0;
        t->slots = slots;
        t->capacity = capacity;
    }
    t->slots[t->count] = item;
    return t->count++;
}

void *D8TableGet(const D8Table *t, UINT index) { return index && index < t->count ? t->slots[index] : nullptr; }

void D8TableRemove(D8Table *t, UINT index)
{
    if (!index || index >= t->count)
        return;
    t->slots[index] = nullptr;
    if (t->free_count == t->free_capacity) {
        UINT capacity = t->free_capacity ? t->free_capacity * 2 : 8;
        UINT *free = t->free ? HeapReAlloc(GetProcessHeap(), 0, t->free, capacity * sizeof(*free))
                             : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*free));
        if (!free)
            return;
        t->free = free;
        t->free_capacity = capacity;
    }
    t->free[t->free_count++] = index;
}

void D8TableFree(D8Table *t)
{
    HeapFree(GetProcessHeap(), 0, t->slots);
    HeapFree(GetProcessHeap(), 0, t->free);
    memset(t, 0, sizeof(*t));
}

/* ------------------------------------------------------------------ declarations */

static const BYTE TYPE_SIZE[8] = {4, 8, 12, 16, 4, 4, 4, 8};

/* the usage of a register of direct3d 8 declarations, as windows hands them to direct3d 9 drivers. */
static void register_usage(UINT reg, BYTE *usage, BYTE *index)
{
    static const BYTE usages[17] = {D3DDECLUSAGE_POSITION, D3DDECLUSAGE_BLENDWEIGHT, D3DDECLUSAGE_BLENDINDICES,
                                    D3DDECLUSAGE_NORMAL,   D3DDECLUSAGE_PSIZE,       D3DDECLUSAGE_COLOR,
                                    D3DDECLUSAGE_COLOR,    D3DDECLUSAGE_TEXCOORD,    D3DDECLUSAGE_TEXCOORD,
                                    D3DDECLUSAGE_TEXCOORD, D3DDECLUSAGE_TEXCOORD,    D3DDECLUSAGE_TEXCOORD,
                                    D3DDECLUSAGE_TEXCOORD, D3DDECLUSAGE_TEXCOORD,    D3DDECLUSAGE_TEXCOORD,
                                    D3DDECLUSAGE_POSITION, D3DDECLUSAGE_NORMAL};
    *usage = usages[reg];
    *index = reg == 6 || reg == 15 || reg == 16 ? 1 : reg >= 7 && reg <= 14 ? reg - 7 : 0;
}

/* the types a fixed function register takes, by bit, and its place in a stream's order. */
static BOOL fixed_type(UINT reg, UINT type)
{
    switch (reg) {
    case 0:
    case 3:
    case 15:
    case 16:
        return type == D3DVSDT_FLOAT3;
    case 1:
        return type <= D3DVSDT_FLOAT4;
    case 2:
        return type == D3DVSDT_UBYTE4;
    case 4:
        return type == D3DVSDT_FLOAT1;
    case 5:
    case 6:
        return type == D3DVSDT_D3DCOLOR;
    default:
        return reg <= 14 && type <= D3DVSDT_FLOAT4;
    }
}

static DWORD fixed_order(UINT reg)
{
    if (reg <= 6)
        return 1u << reg;
    if (reg <= 14)
        return 0x100u << (reg - 7);
    return 0x10000u << (reg - 15);
}

typedef struct D8Parsed {
    D3DVERTEXELEMENT9 elements[18];
    UINT count;
    DWORD registers;
    UINT size;
    D8Constants *constants;
    UINT constant_sets;
} D8Parsed;

static void free_parsed(D8Parsed *p)
{
    for (UINT i = 0; i < p->constant_sets; i++)
        HeapFree(GetProcessHeap(), 0, p->constants[i].values);
    HeapFree(GetProcessHeap(), 0, p->constants);
}

/* the declaration's elements, its constants and its size, with direct3d 8's checks. */
static HRESULT parse_declaration(D8Device *d, const DWORD *decl, BOOL fixed, UINT constant_limit, D8Parsed *out)
{
    memset(out, 0, sizeof(*out));
    UINT streams = D8_MAX(1, D8_MIN(d->caps.MaxStreams, D8_STREAMS));
    int stream = -1;
    BOOL tess = FALSE, tess_seen = FALSE, position = FALSE;
    DWORD seen_streams = 0, order = 0, betas = 0, texcoords = 0;
    UINT offset = 0;
    for (const DWORD *p = decl;; p++) {
        DWORD token = *p;
        UINT kind = token >> 29;
        if (kind == D3DVSD_TOKEN_END) {
            out->size = (UINT)(p - decl + 1) * sizeof(DWORD);
            break;
        }
        switch (kind) {
        case D3DVSD_TOKEN_NOP:
            break;
        case D3DVSD_TOKEN_STREAM:
            if (token & (1u << 28)) {
                if (tess_seen)
                    return D3DERR_INVALIDCALL;
                tess = tess_seen = TRUE;
                stream = 0;
            } else {
                UINT n = token & 0xF;
                if (n >= streams || (seen_streams & (1u << n)))
                    return D3DERR_INVALIDCALL;
                seen_streams |= 1u << n;
                stream = n;
                tess = FALSE;
            }
            offset = 0;
            order = 0;
            break;
        case D3DVSD_TOKEN_STREAMDATA: {
            if (stream < 0)
                return D3DERR_INVALIDCALL;
            if (token & (1u << 28)) {
                if (fixed)
                    return D3DERR_INVALIDCALL;
                offset += 4 * ((token >> 16) & 0xF);
                break;
            }
            UINT reg = token & 0x1F, type = (token >> 16) & 0xF;
            if (type > D3DVSDT_SHORT4 || reg >= (fixed ? 17u : 16u) || (out->registers & (1u << reg)))
                return D3DERR_INVALIDCALL;
            if (fixed) {
                if (!fixed_type(reg, type) || order > fixed_order(reg))
                    return D3DERR_INVALIDCALL;
                order = fixed_order(reg);
                if (reg == 0)
                    position = TRUE;
                else if (reg == 1)
                    betas += type + 1;
                else if (reg == 2)
                    betas++;
                else if (reg >= 7 && reg <= 14)
                    texcoords |= 1u << (reg - 7);
            }
            out->registers |= 1u << reg;
            if (!tess) {
                if (out->count >= 17)
                    return D3DERR_INVALIDCALL;
                D3DVERTEXELEMENT9 *e = &out->elements[out->count++];
                e->Stream = stream;
                e->Offset = offset;
                e->Type = type;
                e->Method = D3DDECLMETHOD_DEFAULT;
                register_usage(reg, &e->Usage, &e->UsageIndex);
            }
            offset += TYPE_SIZE[type];
            break;
        }
        case D3DVSD_TOKEN_TESSELLATOR:
            /* no driver tessellates; the generated inputs are left out. */
            if (!tess)
                return D3DERR_INVALIDCALL;
            break;
        case D3DVSD_TOKEN_CONSTMEM: {
            UINT start = token & 0x7F, count = (token >> 25) & 0xF;
            if (start + count > constant_limit)
                return D3DERR_INVALIDCALL;
            D8Constants *sets = out->constants
                                    ? HeapReAlloc(GetProcessHeap(), 0, out->constants,
                                                  (out->constant_sets + 1) * sizeof(*sets))
                                    : HeapAlloc(GetProcessHeap(), 0, sizeof(*sets));
            if (!sets)
                return E_OUTOFMEMORY;
            out->constants = sets;
            float *values = HeapAlloc(GetProcessHeap(), 0, D8_MAX(count, 1) * 4 * sizeof(float));
            if (!values)
                return E_OUTOFMEMORY;
            memcpy(values, p + 1, count * 4 * sizeof(float));
            sets[out->constant_sets++] = (D8Constants){start, count, values};
            p += 4 * count;
            stream = -1;
            break;
        }
        case D3DVSD_TOKEN_EXT:
            p += (token >> 24) & 0x1F;
            stream = -1;
            break;
        default:
            return D3DERR_INVALIDCALL;
        }
    }
    if (fixed && !tess_seen) {
        if (!position || betas > 5)
            return D3DERR_INVALIDCALL;
        /* texture coordinates must count up from the first set. */
        if (texcoords & (texcoords + 1))
            return D3DERR_INVALIDCALL;
    }
    out->elements[out->count] = (D3DVERTEXELEMENT9)D3DDECL_END();
    return D3D_OK;
}

/* ------------------------------------------------------------------ code */

/* words of a direct3d 8 program up to its end token, and how many of them are not comments. */
static BOOL measure(const DWORD *code, UINT *words, UINT *plain)
{
    UINT at = 1, kept = 1;
    for (;;) {
        if (at > 0x100000)
            return FALSE;
        DWORD token = code[at];
        if (token == 0x0000FFFF) {
            *words = at + 1;
            *plain = kept + 1;
            return TRUE;
        }
        if ((token & 0xFFFF) == 0xFFFE && !(token & 0x80000000)) {
            at += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        UINT length = 1;
        if ((token & 0xFFFF) == 81)
            length = 6;
        else
            while (code[at + length] & 0x80000000)
                length++;
        at += length;
        kept += length;
    }
}

/* the vertex shader with a dcl of each register the declaration feeds, as direct3d 9 wants them declared. */
static DWORD *declared_code(const DWORD *code, UINT words, const D8Parsed *p)
{
    UINT count = 0;
    for (UINT i = 0; i < p->count; i++)
        count++;
    DWORD *out = HeapAlloc(GetProcessHeap(), 0, (words + 3 * count) * sizeof(DWORD));
    if (!out)
        return nullptr;
    UINT at = 0;
    out[at++] = code[0];
    for (UINT reg = 0; reg < 16; reg++) {
        if (!(p->registers & (1u << reg)))
            continue;
        BYTE usage, index;
        register_usage(reg, &usage, &index);
        out[at++] = 0x0300001F;
        out[at++] = 0x80000000 | usage | ((DWORD)index << 16);
        out[at++] = 0x900F0000 | reg;
    }
    memcpy(out + at, code + 1, (words - 1) * sizeof(DWORD));
    return out;
}

static void free_vertex_shader(D8VertexShader *s)
{
    if (s->decl9)
        s->decl9->lpVtbl->Release(s->decl9);
    if (s->vs9)
        s->vs9->lpVtbl->Release(s->vs9);
    for (UINT i = 0; i < s->constant_sets; i++)
        HeapFree(GetProcessHeap(), 0, s->constants[i].values);
    HeapFree(GetProcessHeap(), 0, s->constants);
    HeapFree(GetProcessHeap(), 0, s->declaration);
    HeapFree(GetProcessHeap(), 0, s->function);
    HeapFree(GetProcessHeap(), 0, s);
}

static void free_pixel_shader(D8PixelShader *s)
{
    if (s->ps9)
        s->ps9->lpVtbl->Release(s->ps9);
    for (UINT i = 0; i < s->def_count; i++)
        HeapFree(GetProcessHeap(), 0, s->defs[i].values);
    HeapFree(GetProcessHeap(), 0, s->defs);
    HeapFree(GetProcessHeap(), 0, s->function);
    HeapFree(GetProcessHeap(), 0, s);
}

void D8DeleteShaders(D8Device *d)
{
    for (UINT i = 1; i < d->vertex_shaders.count; i++)
        if (d->vertex_shaders.slots[i])
            free_vertex_shader(d->vertex_shaders.slots[i]);
    for (UINT i = 1; i < d->pixel_shaders.count; i++)
        if (d->pixel_shaders.slots[i])
            free_pixel_shader(d->pixel_shaders.slots[i]);
    D8TableFree(&d->vertex_shaders);
    D8TableFree(&d->pixel_shaders);
    d->vs = d->ps = 0;
}

/* ------------------------------------------------------------------ vertex shaders */

static BOOL software_shader(D8Device *d, DWORD usage)
{
    return (usage & D3DUSAGE_SOFTWAREPROCESSING) || (d->flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING);
}

static HRESULT STDMETHODCALLTYPE device_create_vertex_shader(IDirect3DDevice8 *iface, const DWORD *declaration,
                                                             const DWORD *function, DWORD *handle, DWORD usage)
{
    D8Device *d = impl(iface);
    if (!declaration)
        return D3DERR_INVALIDCALL;
    D3DCAPS8 caps = d->caps;
    caps.MaxStreams = D8_MAX(1, D8_MIN(caps.MaxStreams, D8_STREAMS));
    if (software_shader(d, usage)) {
        caps.VertexShaderVersion = D3DVS_VERSION(1, 1);
        caps.MaxVertexShaderConst = D8_VS_CONSTANTS;
    }
    if (FAILED(D8ValidateVertexShader(function, declaration, &caps, FALSE, nullptr)))
        return D3DERR_INVALIDCALL;
    D8Parsed parsed;
    HRESULT hr = parse_declaration(d, declaration, !function, caps.MaxVertexShaderConst, &parsed);
    UINT words = 0, plain = 0;
    if (SUCCEEDED(hr) && function &&
        (((function[0] >> 16) != 0xFFFE && (function[0] >> 16) != 0xFFFF) || !(function[0] & 0xFFFF) ||
         !measure(function, &words, &plain) || plain > 4096))
        hr = D3DERR_INVALIDCALL;
    D8VertexShader *s = SUCCEEDED(hr) ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s)) : nullptr;
    if (SUCCEEDED(hr) && !s)
        hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        free_parsed(&parsed);
        return hr;
    }
    s->constants = parsed.constants;
    s->constant_sets = parsed.constant_sets;
    s->declaration_size = parsed.size;
    s->declaration = HeapAlloc(GetProcessHeap(), 0, parsed.size);
    if (s->declaration)
        memcpy(s->declaration, declaration, parsed.size);
    else
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && function) {
        s->function_size = words * sizeof(DWORD);
        s->function = HeapAlloc(GetProcessHeap(), 0, s->function_size);
        DWORD *code = declared_code(function, words, &parsed);
        if (!s->function || !code)
            hr = E_OUTOFMEMORY;
        else {
            memcpy(s->function, function, s->function_size);
            hr = d->d9->lpVtbl->CreateVertexShader(d->d9, code, &s->vs9);
        }
        HeapFree(GetProcessHeap(), 0, code);
    }
    if (SUCCEEDED(hr))
        hr = d->d9->lpVtbl->CreateVertexDeclaration(d->d9, parsed.elements, &s->decl9);
    D8Enter(d);
    UINT index = SUCCEEDED(hr) ? D8TableAdd(&d->vertex_shaders, s) : 0;
    D8Leave(d);
    if (!index) {
        free_vertex_shader(s);
        return FAILED(hr) && hr != E_OUTOFMEMORY ? D3DERR_INVALIDCALL : E_OUTOFMEMORY;
    }
    *handle = 2 * index + 1;
    return D3D_OK;
}

HRESULT D8SetVertexConstants(D8Device *d, UINT start, const void *data, UINT count)
{
    if (start >= D8_VS_CONSTANTS || count > D8_VS_CONSTANTS - start)
        return D3DERR_INVALIDCALL;
    memcpy(d->vs_constants[start], data, count * sizeof(d->vs_constants[0]));
    d->d9->lpVtbl->SetVertexShaderConstantF(d->d9, start, data, count);
    return D3D_OK;
}

/* an even value is an fvf code, an odd one a handle, whose declaration's constants load each time. */
HRESULT D8SetVertexShader(D8Device *d, DWORD handle)
{
    IDirect3DDevice9 *d9 = d->d9;
    if (!(handle & 1)) {
        d->vs = handle;
        d9->lpVtbl->SetVertexShader(d9, nullptr);
        d9->lpVtbl->SetFVF(d9, handle);
        return D3D_OK;
    }
    D8VertexShader *s = D8TableGet(&d->vertex_shaders, handle >> 1);
    if (!s)
        return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < s->constant_sets; i++)
        D8SetVertexConstants(d, s->constants[i].start, s->constants[i].values, s->constants[i].count);
    d9->lpVtbl->SetVertexDeclaration(d9, s->decl9);
    d9->lpVtbl->SetVertexShader(d9, s->vs9);
    d->vs = handle;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_vertex_shader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8Device *d = impl(iface);
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_VS);
        if (!c)
            return E_OUTOFMEMORY;
        c->u.value = handle;
        return D3D_OK;
    }
    D8Enter(d);
    HRESULT hr = D8SetVertexShader(d, handle);
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_shader(IDirect3DDevice8 *iface, DWORD *handle)
{
    *handle = impl(iface)->vs;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_delete_vertex_shader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8Device *d = impl(iface);
    D8Enter(d);
    D8VertexShader *s = D8TableGet(&d->vertex_shaders, handle >> 1);
    if (!s) {
        D8Leave(d);
        return D3DERR_INVALIDCALL;
    }
    if (d->vs == handle) {
        d->vs = 0;
        d->d9->lpVtbl->SetVertexShader(d->d9, nullptr);
        d->d9->lpVtbl->SetFVF(d->d9, 0);
    }
    free_vertex_shader(s);
    D8TableRemove(&d->vertex_shaders, handle >> 1);
    D8Leave(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_vertex_shader_constant(IDirect3DDevice8 *iface, DWORD reg,
                                                                   const void *data, DWORD count)
{
    D8Device *d = impl(iface);
    if (!data || reg >= D8_VS_CONSTANTS || count > D8_VS_CONSTANTS - reg)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_VS_CONSTANTS);
        if (!c || !(c->constants = HeapAlloc(GetProcessHeap(), 0, D8_MAX(count, 1) * 4 * sizeof(float))))
            return E_OUTOFMEMORY;
        c->a = reg;
        c->b = count;
        memcpy(c->constants, data, count * 4 * sizeof(float));
        return D3D_OK;
    }
    return D8SetVertexConstants(d, reg, data, count);
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_shader_constant(IDirect3DDevice8 *iface, DWORD reg, void *data,
                                                                   DWORD count)
{
    D8Device *d = impl(iface);
    if (reg >= D8_VS_CONSTANTS || count > D8_VS_CONSTANTS - reg)
        return D3DERR_INVALIDCALL;
    memcpy(data, d->vs_constants[reg], count * sizeof(d->vs_constants[0]));
    return D3D_OK;
}

/* a copy of bytes kept, or its size when no buffer is given. */
static HRESULT give_bytes(const void *bytes, UINT size, void *data, DWORD *data_size)
{
    if (!data) {
        *data_size = size;
        return D3D_OK;
    }
    if (*data_size < size)
        return D3DERR_INVALIDCALL;
    memcpy(data, bytes, size);
    *data_size = size;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_shader_declaration(IDirect3DDevice8 *iface, DWORD handle,
                                                                      void *data, DWORD *size)
{
    D8Device *d = impl(iface);
    if (!(handle & 1)) {
        *size = 0;
        return D3DERR_INVALIDCALL;
    }
    D8VertexShader *s = D8TableGet(&d->vertex_shaders, handle >> 1);
    return s ? give_bytes(s->declaration, s->declaration_size, data, size) : D3DERR_INVALIDCALL;
}

static HRESULT STDMETHODCALLTYPE device_get_vertex_shader_function(IDirect3DDevice8 *iface, DWORD handle,
                                                                   void *data, DWORD *size)
{
    D8Device *d = impl(iface);
    if (!(handle & 1)) {
        *size = 0;
        return D3DERR_INVALIDCALL;
    }
    D8VertexShader *s = D8TableGet(&d->vertex_shaders, handle >> 1);
    return s ? give_bytes(s->function, s->function_size, data, size) : D3DERR_INVALIDCALL;
}

/* ------------------------------------------------------------------ pixel shaders */

/* the def instructions at the start of a pixel shader, before anything else may come. */
static BOOL find_defs(D8PixelShader *s, const DWORD *code, UINT words)
{
    for (UINT at = 1; at < words;) {
        DWORD token = code[at];
        if ((token & 0xFFFF) == 0xFFFE && !(token & 0x80000000)) {
            at += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        if ((token & 0xFFFF) != 81 || at + 6 > words)
            break;
        D8Constants *defs = s->defs ? HeapReAlloc(GetProcessHeap(), 0, s->defs, (s->def_count + 1) * sizeof(*defs))
                                    : HeapAlloc(GetProcessHeap(), 0, sizeof(*defs));
        float *values = HeapAlloc(GetProcessHeap(), 0, 4 * sizeof(float));
        if (defs)
            s->defs = defs;
        if (!defs || !values) {
            HeapFree(GetProcessHeap(), 0, values);
            return FALSE;
        }
        memcpy(values, code + at + 2, 4 * sizeof(float));
        defs[s->def_count++] = (D8Constants){code[at + 1] & 0x7FF, 1, values};
        at += 6;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE device_create_pixel_shader(IDirect3DDevice8 *iface, const DWORD *function,
                                                            DWORD *handle)
{
    D8Device *d = impl(iface);
    UINT words, plain;
    if (!function || FAILED(D8ValidatePixelShader(function, &d->caps, FALSE, nullptr)) ||
        !measure(function, &words, &plain))
        return D3DERR_INVALIDCALL;
    D8PixelShader *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    HRESULT hr = D3D_OK;
    s->function_size = words * sizeof(DWORD);
    s->function = HeapAlloc(GetProcessHeap(), 0, s->function_size);
    if (!s->function || !find_defs(s, function, words))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        memcpy(s->function, function, s->function_size);
        if (FAILED(d->d9->lpVtbl->CreatePixelShader(d->d9, function, &s->ps9)))
            hr = D3DERR_INVALIDCALL;
    }
    D8Enter(d);
    UINT index = SUCCEEDED(hr) ? D8TableAdd(&d->pixel_shaders, s) : 0;
    D8Leave(d);
    if (!index) {
        free_pixel_shader(s);
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    *handle = index;
    return D3D_OK;
}

HRESULT D8SetPixelConstants(D8Device *d, UINT start, const void *data, UINT count)
{
    if (start >= D8_PS_CONSTANTS || count > D8_PS_CONSTANTS - start)
        return D3DERR_INVALIDCALL;
    memcpy(d->ps_constants[start], data, count * sizeof(d->ps_constants[0]));
    d->d9->lpVtbl->SetPixelShaderConstantF(d->d9, start, data, count);
    return D3D_OK;
}

/* setting a shader writes its defs over the constants, where programs read them back. */
HRESULT D8SetPixelShader(D8Device *d, DWORD handle)
{
    IDirect3DDevice9 *d9 = d->d9;
    D8PixelShader *s = handle ? D8TableGet(&d->pixel_shaders, handle) : nullptr;
    if (!s) {
        d->ps = 0;
        d9->lpVtbl->SetPixelShader(d9, nullptr);
        return handle ? E_FAIL : D3D_OK;
    }
    for (UINT i = 0; i < s->def_count; i++)
        D8SetPixelConstants(d, s->defs[i].start, s->defs[i].values, 1);
    d9->lpVtbl->SetPixelShader(d9, s->ps9);
    d->ps = handle;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_pixel_shader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8Device *d = impl(iface);
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_PS);
        if (!c)
            return E_OUTOFMEMORY;
        c->u.value = handle;
        return D3D_OK;
    }
    D8Enter(d);
    HRESULT hr = D8SetPixelShader(d, handle);
    D8Leave(d);
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_get_pixel_shader(IDirect3DDevice8 *iface, DWORD *handle)
{
    *handle = impl(iface)->ps;
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_delete_pixel_shader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8Device *d = impl(iface);
    D8Enter(d);
    D8PixelShader *s = D8TableGet(&d->pixel_shaders, handle);
    if (!s) {
        D8Leave(d);
        return D3DERR_INVALIDCALL;
    }
    if (d->ps == handle) {
        d->ps = 0;
        d->d9->lpVtbl->SetPixelShader(d->d9, nullptr);
    }
    free_pixel_shader(s);
    D8TableRemove(&d->pixel_shaders, handle);
    D8Leave(d);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_pixel_shader_constant(IDirect3DDevice8 *iface, DWORD reg,
                                                                  const void *data, DWORD count)
{
    D8Device *d = impl(iface);
    if (!data || reg >= D8_PS_CONSTANTS || count > D8_PS_CONSTANTS - reg)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_PS_CONSTANTS);
        if (!c || !(c->constants = HeapAlloc(GetProcessHeap(), 0, D8_MAX(count, 1) * 4 * sizeof(float))))
            return E_OUTOFMEMORY;
        c->a = reg;
        c->b = count;
        memcpy(c->constants, data, count * 4 * sizeof(float));
        return D3D_OK;
    }
    return D8SetPixelConstants(d, reg, data, count);
}

static HRESULT STDMETHODCALLTYPE device_get_pixel_shader_constant(IDirect3DDevice8 *iface, DWORD reg, void *data,
                                                                  DWORD count)
{
    D8Device *d = impl(iface);
    if (reg >= D8_PS_CONSTANTS || count > D8_PS_CONSTANTS - reg)
        return D3DERR_INVALIDCALL;
    memcpy(data, d->ps_constants[reg], count * sizeof(d->ps_constants[0]));
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_get_pixel_shader_function(IDirect3DDevice8 *iface, DWORD handle, void *data,
                                                                  DWORD *size)
{
    D8PixelShader *s = D8TableGet(&impl(iface)->pixel_shaders, handle);
    return s ? give_bytes(s->function, s->function_size, data, size) : D3DERR_INVALIDCALL;
}

/* ------------------------------------------------------------------ streams and indices */

HRESULT D8SetStreamSource(D8Device *d, UINT stream, D8Object *buffer, UINT stride)
{
    if (stream >= D8_STREAMS)
        return D3DERR_INVALIDCALL;
    /* clearing a stream keeps its stride. */
    if (buffer)
        d->streams[stream].stride = stride;
    D8Bind(&d->streams[stream].buffer, buffer);
    d->d9->lpVtbl->SetStreamSource(d->d9, stream, buffer ? (IDirect3DVertexBuffer9 *)buffer->d9 : nullptr, 0,
                                   d->streams[stream].stride);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_stream_source(IDirect3DDevice8 *iface, UINT stream,
                                                          IDirect3DVertexBuffer8 *buffer, UINT stride)
{
    D8Device *d = impl(iface);
    D8Object *b = D8Check(d, buffer, D8_KIND(D8_VERTEX_BUFFER));
    if (stream >= D8_MIN(d->caps.MaxStreams, D8_STREAMS) || (buffer && !b))
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_STREAM);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = stream;
        c->b = stride;
        D8Bind(&c->object, b);
        return D3D_OK;
    }
    if (d->streams[stream].buffer == b && d->streams[stream].stride == stride)
        return D3D_OK;
    return D8SetStreamSource(d, stream, b, stride);
}

static HRESULT STDMETHODCALLTYPE device_get_stream_source(IDirect3DDevice8 *iface, UINT stream,
                                                          IDirect3DVertexBuffer8 **out, UINT *stride)
{
    D8Device *d = impl(iface);
    if (stream >= D8_STREAMS)
        return D3DERR_INVALIDCALL;
    D8Object *b = d->streams[stream].buffer;
    *out = b ? D8_IFACE(b) : nullptr;
    if (b)
        D8ObjectAddRef(b);
    *stride = d->streams[stream].stride;
    return D3D_OK;
}

HRESULT D8SetIndices(D8Device *d, D8Object *buffer, UINT base)
{
    if (buffer || d->indices == buffer)
        d->base_vertex = base;
    if (d->indices == buffer)
        return D3D_OK;
    D8Bind(&d->indices, buffer);
    d->d9->lpVtbl->SetIndices(d->d9, buffer ? (IDirect3DIndexBuffer9 *)buffer->d9 : nullptr);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE device_set_indices(IDirect3DDevice8 *iface, IDirect3DIndexBuffer8 *buffer,
                                                    UINT base)
{
    D8Device *d = impl(iface);
    D8Object *b = D8Check(d, buffer, D8_KIND(D8_INDEX_BUFFER));
    if (buffer && !b)
        return D3DERR_INVALIDCALL;
    if (d->recording) {
        D8Command *c = D8Record(d, D8_CMD_INDICES);
        if (!c)
            return E_OUTOFMEMORY;
        c->a = base;
        D8Bind(&c->object, b);
        return D3D_OK;
    }
    return D8SetIndices(d, b, base);
}

static HRESULT STDMETHODCALLTYPE device_get_indices(IDirect3DDevice8 *iface, IDirect3DIndexBuffer8 **out, UINT *base)
{
    D8Device *d = impl(iface);
    *out = d->indices ? D8_IFACE(d->indices) : nullptr;
    if (d->indices)
        D8ObjectAddRef(d->indices);
    *base = d->base_vertex;
    return D3D_OK;
}

void D8SwitchProcessing(D8Device *d, BOOL software)
{
    if (d->software == software)
        return;
    d->software = software;
    for (UINT i = 0; i < D8_STREAMS; i++)
        if (d->streams[i].buffer)
            D8SetStreamSource(d, i, nullptr, 0);
    D8SetIndices(d, nullptr, 0);
    D8SetVertexShader(d, 0);
    d->d9->lpVtbl->SetSoftwareVertexProcessing(d->d9, software);
}

/* ------------------------------------------------------------------ drawing */

static HRESULT STDMETHODCALLTYPE device_draw_primitive(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE type, UINT start,
                                                       UINT count)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return d9->lpVtbl->DrawPrimitive(d9, type, start, count);
}

/* indices count from the base vertex SetIndices gave. */
static HRESULT STDMETHODCALLTYPE device_draw_indexed_primitive(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE type,
                                                               UINT min_index, UINT vertex_count, UINT start_index,
                                                               UINT count)
{
    D8Device *d = impl(iface);
    return d->d9->lpVtbl->DrawIndexedPrimitive(d->d9, type, d->base_vertex, min_index, vertex_count, start_index,
                                               count);
}

/* drawing from memory leaves stream 0 empty, its stride 0. */
static void clear_stream0(D8Device *d)
{
    D8Bind(&d->streams[0].buffer, nullptr);
    d->streams[0].stride = 0;
}

static HRESULT STDMETHODCALLTYPE device_draw_primitive_up(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE type, UINT count,
                                                          const void *data, UINT stride)
{
    D8Device *d = impl(iface);
    clear_stream0(d);
    return d->d9->lpVtbl->DrawPrimitiveUP(d->d9, type, count, data, stride);
}

static HRESULT STDMETHODCALLTYPE device_draw_indexed_primitive_up(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE type,
                                                                  UINT min_index, UINT vertex_count, UINT count,
                                                                  const void *indices, D3DFORMAT format,
                                                                  const void *data, UINT stride)
{
    D8Device *d = impl(iface);
    clear_stream0(d);
    D8Bind(&d->indices, nullptr);
    d->base_vertex = 0;
    if (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32) {
        d->d9->lpVtbl->SetStreamSource(d->d9, 0, nullptr, 0, 0);
        d->d9->lpVtbl->SetIndices(d->d9, nullptr);
        return D3DERR_INVALIDCALL;
    }
    return d->d9->lpVtbl->DrawIndexedPrimitiveUP(d->d9, type, min_index, vertex_count, count, indices, format, data,
                                                 stride);
}

static HRESULT STDMETHODCALLTYPE device_process_vertices(IDirect3DDevice8 *iface, UINT src_start, UINT dst_index,
                                                         UINT count, IDirect3DVertexBuffer8 *dest, DWORD flags)
{
    D8Device *d = impl(iface);
    D8Object *b = D8Check(d, dest, D8_KIND(D8_VERTEX_BUFFER));
    if (!b)
        return D3DERR_INVALIDCALL;
    return d->d9->lpVtbl->ProcessVertices(d->d9, src_start, dst_index, count, (IDirect3DVertexBuffer9 *)b->d9,
                                          nullptr, flags);
}

static HRESULT STDMETHODCALLTYPE device_draw_rect_patch(IDirect3DDevice8 *iface, UINT handle, const float *segments,
                                                        const D3DRECTPATCH_INFO *info)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return d9->lpVtbl->DrawRectPatch(d9, handle, segments, info);
}

static HRESULT STDMETHODCALLTYPE device_draw_tri_patch(IDirect3DDevice8 *iface, UINT handle, const float *segments,
                                                       const D3DTRIPATCH_INFO *info)
{
    IDirect3DDevice9 *d9 = impl(iface)->d9;
    return d9->lpVtbl->DrawTriPatch(d9, handle, segments, info);
}

static HRESULT STDMETHODCALLTYPE device_delete_patch(IDirect3DDevice8 *iface, UINT handle)
{
    D8Device *d = impl(iface);
    if (!(d->caps.DevCaps & D3DDEVCAPS_RTPATCHES))
        return D3DERR_INVALIDCALL;
    return handle ? d->d9->lpVtbl->DeletePatch(d->d9, handle) : D3D_OK;
}

void D8DeviceShaderMethods(IDirect3DDevice8Vtbl *v)
{
    v->CreateVertexShader = device_create_vertex_shader;
    v->SetVertexShader = device_set_vertex_shader;
    v->GetVertexShader = device_get_vertex_shader;
    v->DeleteVertexShader = device_delete_vertex_shader;
    v->SetVertexShaderConstant = device_set_vertex_shader_constant;
    v->GetVertexShaderConstant = device_get_vertex_shader_constant;
    v->GetVertexShaderDeclaration = device_get_vertex_shader_declaration;
    v->GetVertexShaderFunction = device_get_vertex_shader_function;
    v->CreatePixelShader = device_create_pixel_shader;
    v->SetPixelShader = device_set_pixel_shader;
    v->GetPixelShader = device_get_pixel_shader;
    v->DeletePixelShader = device_delete_pixel_shader;
    v->SetPixelShaderConstant = device_set_pixel_shader_constant;
    v->GetPixelShaderConstant = device_get_pixel_shader_constant;
    v->GetPixelShaderFunction = device_get_pixel_shader_function;
    v->SetStreamSource = device_set_stream_source;
    v->GetStreamSource = device_get_stream_source;
    v->SetIndices = device_set_indices;
    v->GetIndices = device_get_indices;
    v->DrawPrimitive = device_draw_primitive;
    v->DrawIndexedPrimitive = device_draw_indexed_primitive;
    v->DrawPrimitiveUP = device_draw_primitive_up;
    v->DrawIndexedPrimitiveUP = device_draw_indexed_primitive_up;
    v->ProcessVertices = device_process_vertices;
    v->DrawRectPatch = device_draw_rect_patch;
    v->DrawTriPatch = device_draw_tri_patch;
    v->DeletePatch = device_delete_patch;
}
