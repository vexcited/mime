/*
 * D3D10ReflectShader: ID3D10ShaderReflection over a shader model 4
 * container, as d3d10.dll's CShaderReflection reads it: the RDEF chunk
 * when there is one, then STAT, ISGN, OSGN and SHDR, which must be there.
 * what is out of range or not found is one of the invalid objects, whose
 * GetDesc fails with E_FAIL.
 */
#include "d3d10_private.h"
#include "../lib/dxbc/dxbc.h"

#define VTBL_FN(f) ((void *)(f))

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }
static UINT read16(const BYTE *p) { return p[0] | p[1] << 8; }

typedef struct RType RType;

struct RType {
    ID3D10ShaderReflectionType iface;
    D3D10_SHADER_TYPE_DESC desc;
    const char **names;
    RType *members;
};

typedef struct RVariable {
    ID3D10ShaderReflectionVariable iface;
    D3D10_SHADER_VARIABLE_DESC desc;
    RType *type;
} RVariable;

typedef struct RBuffer {
    ID3D10ShaderReflectionConstantBuffer iface;
    D3D10_SHADER_BUFFER_DESC desc;
    RVariable *variables;
} RBuffer;

typedef struct RBlock {
    struct RBlock *next;
} RBlock;

typedef struct Reflection {
    ID3D10ShaderReflection iface;
    LONG refs;
    D3D10_SHADER_DESC desc;
    UINT program;
    RBuffer *buffers;
    D3D10_SHADER_INPUT_BIND_DESC *bindings;
    D3D10_SIGNATURE_PARAMETER_DESC *inputs, *outputs;
    /* the container, which names point into. */
    BYTE *code;
    const BYTE *end;
    RBlock *blocks;
} Reflection;

static void *alloc(Reflection *r, SIZE_T size)
{
    RBlock *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RBlock) + size + 8);
    if (!block)
        return nullptr;
    block->next = r->blocks;
    r->blocks = block;
    return (BYTE *)block + ((sizeof(RBlock) + 7) & ~7);
}

/* ------------------------------------------------------------------ invalid objects */

static HRESULT STDMETHODCALLTYPE invalid_desc(void *iface, void *desc) { return E_FAIL; }

static ID3D10ShaderReflectionType invalid_type;
static ID3D10ShaderReflectionVariable invalid_variable;

static ID3D10ShaderReflectionType *STDMETHODCALLTYPE invalid_member(void *iface, const void *which)
{
    return &invalid_type;
}

static LPCSTR STDMETHODCALLTYPE invalid_member_name(void *iface, UINT index) { return "$Invalid"; }

static ID3D10ShaderReflectionType *STDMETHODCALLTYPE invalid_get_type(void *iface) { return &invalid_type; }

static ID3D10ShaderReflectionVariable *STDMETHODCALLTYPE invalid_get_variable(void *iface, const void *which)
{
    return &invalid_variable;
}

static const ID3D10ShaderReflectionTypeVtbl invalid_type_vtbl = {VTBL_FN(invalid_desc), VTBL_FN(invalid_member),
                                                                 VTBL_FN(invalid_member), VTBL_FN(invalid_member_name)};
static const ID3D10ShaderReflectionVariableVtbl invalid_variable_vtbl = {VTBL_FN(invalid_desc),
                                                                         VTBL_FN(invalid_get_type)};
static const ID3D10ShaderReflectionConstantBufferVtbl invalid_buffer_vtbl = {
    VTBL_FN(invalid_desc), VTBL_FN(invalid_get_variable), VTBL_FN(invalid_get_variable)};

static ID3D10ShaderReflectionType invalid_type = {&invalid_type_vtbl};
static ID3D10ShaderReflectionVariable invalid_variable = {&invalid_variable_vtbl};
static ID3D10ShaderReflectionConstantBuffer invalid_buffer = {&invalid_buffer_vtbl};

/* ------------------------------------------------------------------ types */

static HRESULT STDMETHODCALLTYPE type_get_desc(ID3D10ShaderReflectionType *iface, D3D10_SHADER_TYPE_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = ((RType *)iface)->desc;
    return S_OK;
}

static ID3D10ShaderReflectionType *STDMETHODCALLTYPE type_member_by_index(ID3D10ShaderReflectionType *iface, UINT index)
{
    RType *t = (RType *)iface;
    return index < t->desc.Members ? &t->members[index].iface : &invalid_type;
}

static ID3D10ShaderReflectionType *STDMETHODCALLTYPE type_member_by_name(ID3D10ShaderReflectionType *iface, LPCSTR name)
{
    RType *t = (RType *)iface;
    for (UINT i = 0; name && i < t->desc.Members; i++)
        if (!strcmp(t->names[i], name))
            return &t->members[i].iface;
    return &invalid_type;
}

static LPCSTR STDMETHODCALLTYPE type_member_name(ID3D10ShaderReflectionType *iface, UINT index)
{
    RType *t = (RType *)iface;
    return index < t->desc.Members ? t->names[index] : nullptr;
}

static const ID3D10ShaderReflectionTypeVtbl type_vtbl = {type_get_desc, type_member_by_index, type_member_by_name,
                                                         type_member_name};

/* ------------------------------------------------------------------ variables and buffers */

static HRESULT STDMETHODCALLTYPE variable_get_desc(ID3D10ShaderReflectionVariable *iface,
                                                   D3D10_SHADER_VARIABLE_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = ((RVariable *)iface)->desc;
    return S_OK;
}

static ID3D10ShaderReflectionType *STDMETHODCALLTYPE variable_get_type(ID3D10ShaderReflectionVariable *iface)
{
    return &((RVariable *)iface)->type->iface;
}

static const ID3D10ShaderReflectionVariableVtbl variable_vtbl = {variable_get_desc, variable_get_type};

static HRESULT STDMETHODCALLTYPE buffer_get_desc(ID3D10ShaderReflectionConstantBuffer *iface,
                                                 D3D10_SHADER_BUFFER_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = ((RBuffer *)iface)->desc;
    return S_OK;
}

static ID3D10ShaderReflectionVariable *STDMETHODCALLTYPE
buffer_variable_by_index(ID3D10ShaderReflectionConstantBuffer *iface, UINT index)
{
    RBuffer *b = (RBuffer *)iface;
    return index < b->desc.Variables ? &b->variables[index].iface : &invalid_variable;
}

static ID3D10ShaderReflectionVariable *STDMETHODCALLTYPE
buffer_variable_by_name(ID3D10ShaderReflectionConstantBuffer *iface, LPCSTR name)
{
    RBuffer *b = (RBuffer *)iface;
    for (UINT i = 0; name && i < b->desc.Variables; i++)
        if (!strcmp(b->variables[i].desc.Name, name))
            return &b->variables[i].iface;
    return &invalid_variable;
}

static const ID3D10ShaderReflectionConstantBufferVtbl buffer_vtbl = {buffer_get_desc, buffer_variable_by_index,
                                                                     buffer_variable_by_name};

/* ------------------------------------------------------------------ reading */

/* a name inside the chunk, NULL when it runs past the container. */
static const char *string_at(Reflection *r, const BYTE *chunk, UINT offset)
{
    const BYTE *s = chunk + offset;
    if (s < chunk || s >= r->end)
        return nullptr;
    for (const BYTE *p = s; p < r->end; p++)
        if (!*p)
            return (const char *)s;
    return nullptr;
}

/* whether count entries of size bytes at offset fit in the container. */
static BOOL fits(Reflection *r, const BYTE *chunk, UINT offset, UINT count, UINT size)
{
    const BYTE *p = chunk + offset;
    return p >= chunk && p <= r->end && (UINT64)count * size <= (UINT64)(r->end - p);
}

static HRESULT read_type(Reflection *r, const BYTE *chunk, UINT offset, RType *t)
{
    if (!fits(r, chunk, offset, 1, 16))
        return E_FAIL;
    const BYTE *info = chunk + offset;
    t->iface.lpVtbl = &type_vtbl;
    t->desc.Class = (D3D10_SHADER_VARIABLE_CLASS)read16(info);
    t->desc.Type = (D3D10_SHADER_VARIABLE_TYPE)read16(info + 2);
    t->desc.Rows = read16(info + 4);
    t->desc.Columns = read16(info + 6);
    t->desc.Elements = read16(info + 8);
    t->desc.Members = read16(info + 10);
    UINT count = t->desc.Members, members = read32(info + 12);
    if (!count)
        return S_OK;
    if (!fits(r, chunk, members, count, 12))
        return E_FAIL;
    t->names = alloc(r, count * sizeof(*t->names));
    t->members = alloc(r, count * sizeof(*t->members));
    if (!t->names || !t->members)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        const BYTE *m = chunk + members + 12 * i;
        if (!(t->names[i] = string_at(r, chunk, read32(m))))
            return E_FAIL;
        HRESULT hr = read_type(r, chunk, read32(m + 4), &t->members[i]);
        if (FAILED(hr))
            return hr;
        t->members[i].desc.Offset = read32(m + 8);
    }
    return S_OK;
}

static HRESULT read_variable(Reflection *r, const BYTE *chunk, const BYTE *def, RVariable *v)
{
    v->iface.lpVtbl = &variable_vtbl;
    if (!(v->desc.Name = string_at(r, chunk, read32(def))))
        return E_FAIL;
    v->desc.StartOffset = read32(def + 4);
    v->desc.Size = read32(def + 8);
    v->desc.uFlags = read32(def + 12) & 3;
    UINT value = read32(def + 20);
    if (v->desc.Size && value) {
        if (!fits(r, chunk, value, 1, v->desc.Size))
            return E_FAIL;
        v->desc.DefaultValue = (void *)(chunk + value);
    }
    if (!(v->type = alloc(r, sizeof(*v->type))))
        return E_OUTOFMEMORY;
    return read_type(r, chunk, read32(def + 16), v->type);
}

static HRESULT read_resources(Reflection *r, const BYTE *chunk)
{
    if (!fits(r, chunk, 0, 1, 28))
        return E_FAIL;
    r->desc.Flags = read32(chunk + 20);
    if (!(r->desc.Creator = string_at(r, chunk, read32(chunk + 24))))
        return E_FAIL;
    UINT count = read32(chunk), offset = read32(chunk + 4);
    if (count) {
        r->desc.ConstantBuffers = count;
        if (!fits(r, chunk, offset, count, 24))
            return E_FAIL;
        if (!(r->buffers = alloc(r, count * sizeof(*r->buffers))))
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < count; i++) {
            const BYTE *cb = chunk + offset + 24 * i;
            RBuffer *b = &r->buffers[i];
            b->iface.lpVtbl = &buffer_vtbl;
            if (!(b->desc.Name = string_at(r, chunk, read32(cb))))
                return E_FAIL;
            b->desc.Type = (D3D10_CBUFFER_TYPE)read32(cb + 20);
            b->desc.Size = read32(cb + 12);
            b->desc.Variables = read32(cb + 4);
            b->desc.uFlags = read32(cb + 16) & 1;
            UINT vars = read32(cb + 8);
            if (!b->desc.Variables)
                continue;
            if (!fits(r, chunk, vars, b->desc.Variables, 24))
                return E_FAIL;
            if (!(b->variables = alloc(r, b->desc.Variables * sizeof(*b->variables))))
                return E_OUTOFMEMORY;
            for (UINT j = 0; j < b->desc.Variables; j++) {
                HRESULT hr = read_variable(r, chunk, chunk + vars + 24 * j, &b->variables[j]);
                if (FAILED(hr))
                    return hr;
            }
        }
    }
    count = read32(chunk + 8);
    offset = read32(chunk + 12);
    if (!count)
        return S_OK;
    r->desc.BoundResources = count;
    if (!fits(r, chunk, offset, count, 32))
        return E_FAIL;
    if (!(r->bindings = alloc(r, count * sizeof(*r->bindings))))
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        const BYTE *e = chunk + offset + 32 * i;
        D3D10_SHADER_INPUT_BIND_DESC *d = &r->bindings[i];
        if (!(d->Name = string_at(r, chunk, read32(e))))
            return E_FAIL;
        d->Type = (D3D10_SHADER_INPUT_TYPE)read32(e + 4);
        d->ReturnType = (D3D10_RESOURCE_RETURN_TYPE)read32(e + 8);
        d->Dimension = (D3D_SRV_DIMENSION)read32(e + 12);
        d->NumSamples = read32(e + 16);
        d->BindPoint = read32(e + 20);
        d->BindCount = read32(e + 24);
        d->uFlags = read32(e + 28) & 3;
    }
    return S_OK;
}

/* a pixel shader's outputs without a system value named SV_TARGET or SV_DEPTH, in capitals, get theirs. */
static HRESULT read_signature(Reflection *r, const BYTE *chunk, D3D10_SIGNATURE_PARAMETER_DESC **out, UINT *total)
{
    if (!fits(r, chunk, 0, 1, 8))
        return E_FAIL;
    UINT count = read32(chunk), offset = read32(chunk + 4);
    if (!count)
        return S_OK;
    *total = count;
    if (!fits(r, chunk, offset, count, 24))
        return E_FAIL;
    if (!(*out = alloc(r, count * sizeof(**out))))
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        const BYTE *e = chunk + offset + 24 * i;
        D3D10_SIGNATURE_PARAMETER_DESC *d = &(*out)[i];
        if (!(d->SemanticName = string_at(r, chunk, read32(e))))
            return E_FAIL;
        d->SemanticIndex = read32(e + 4);
        d->SystemValueType = (D3D10_NAME)read32(e + 8);
        d->ComponentType = (D3D10_REGISTER_COMPONENT_TYPE)read32(e + 12);
        d->Register = read32(e + 16);
        d->Mask = e[20];
        d->ReadWriteMask = e[21];
        if (!d->SystemValueType && r->program == DXBC_PIXEL) {
            if (!strcmp(d->SemanticName, "SV_TARGET"))
                d->SystemValueType = D3D_NAME_TARGET;
            else if (!strcmp(d->SemanticName, "SV_DEPTH"))
                d->SystemValueType = D3D_NAME_DEPTH;
        }
    }
    return S_OK;
}

/* the version, and a geometry shader's output topology and vertex count, from its declarations. */
static HRESULT read_code(Reflection *r, const BYTE *code)
{
    if (!fits(r, code, 0, 1, 8))
        return E_FAIL;
    r->program = read16(code + 2);
    r->desc.Version = code[0] | r->program << 16;
    UINT length = read32(code + 4);
    const BYTE *end = (UINT64)length * 4 <= (UINT64)(r->end - code) ? code + 4 * length : r->end;
    for (const BYTE *p = code + 8; p + 4 <= end;) {
        UINT token = read32(p), op = token & 0x7FF, size = (token >> 24) & 0x7F;
        if (op == 53)
            size = p + 8 <= end ? read32(p + 4) : 0;
        if (op == 92) {
            UINT topology = (token >> 11) & 0x3F;
            if (topology == 1 || topology == 3 || topology == 5)
                r->desc.GSOutputTopology = (D3D_PRIMITIVE_TOPOLOGY)topology;
        } else if (op == 94 && p + 8 <= end) {
            r->desc.GSMaxOutputVertexCount = read32(p + 4);
        }
        if (!size)
            break;
        p += 4 * size;
    }
    return S_OK;
}

/* the statistics chunk, in its own order. */
static void read_statistics(Reflection *r, const BYTE *stat, UINT size)
{
    UINT *const fields[] = {&r->desc.InstructionCount,
                            &r->desc.TempRegisterCount,
                            &r->desc.DefCount,
                            &r->desc.DclCount,
                            &r->desc.FloatInstructionCount,
                            &r->desc.IntInstructionCount,
                            &r->desc.UintInstructionCount,
                            &r->desc.StaticFlowControlCount,
                            &r->desc.DynamicFlowControlCount,
                            &r->desc.MacroInstructionCount,
                            &r->desc.TempArrayCount,
                            &r->desc.ArrayInstructionCount,
                            &r->desc.CutInstructionCount,
                            &r->desc.EmitInstructionCount,
                            &r->desc.TextureNormalInstructions,
                            &r->desc.TextureLoadInstructions,
                            &r->desc.TextureCompInstructions,
                            &r->desc.TextureBiasInstructions,
                            &r->desc.TextureGradientInstructions};
    for (UINT i = 0; i < RTL_NUMBER_OF(fields) && 4 * i + 4 <= size; i++)
        *fields[i] = read32(stat + 4 * i);
}

static HRESULT initialize(Reflection *r, const void *bytecode, SIZE_T length)
{
    if (!bytecode || !length || !D3d10ContainerValid(bytecode, length))
        return E_FAIL;
    if (!(r->code = alloc(r, length)))
        return E_OUTOFMEMORY;
    memcpy(r->code, bytecode, length);
    r->end = r->code + length;
    uint32_t rdef_size, isgn_size, osgn_size, stat_size, shdr_size;
    const BYTE *rdef = dxbc_find_chunk(r->code, length, DXBC_FOURCC('R', 'D', 'E', 'F'), &rdef_size);
    const BYTE *isgn = dxbc_find_chunk(r->code, length, DXBC_FOURCC('I', 'S', 'G', 'N'), &isgn_size);
    const BYTE *osgn = dxbc_find_chunk(r->code, length, DXBC_FOURCC('O', 'S', 'G', 'N'), &osgn_size);
    const BYTE *stat = dxbc_find_chunk(r->code, length, DXBC_FOURCC('S', 'T', 'A', 'T'), &stat_size);
    const BYTE *shdr = dxbc_find_chunk(r->code, length, DXBC_FOURCC('S', 'H', 'D', 'R'), &shdr_size);
    if (!shdr)
        return E_FAIL;
    HRESULT hr = read_code(r, shdr);
    if (SUCCEEDED(hr) && rdef)
        hr = read_resources(r, rdef);
    if (FAILED(hr))
        return hr;
    if (!stat || !isgn || !osgn)
        return E_FAIL;
    read_statistics(r, stat, stat_size);
    hr = read_signature(r, isgn, &r->inputs, &r->desc.InputParameters);
    if (SUCCEEDED(hr))
        hr = read_signature(r, osgn, &r->outputs, &r->desc.OutputParameters);
    return hr;
}

/* ------------------------------------------------------------------ the reflection */

static Reflection *impl(ID3D10ShaderReflection *iface) { return CONTAINING_RECORD(iface, Reflection, iface); }

static void destroy(Reflection *r)
{
    for (RBlock *b = r->blocks, *next; b; b = next) {
        next = b->next;
        HeapFree(GetProcessHeap(), 0, b);
    }
    HeapFree(GetProcessHeap(), 0, r);
}

static HRESULT STDMETHODCALLTYPE reflection_query(ID3D10ShaderReflection *iface, REFIID riid, void **out)
{
    if (!out)
        return E_FAIL;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D10ShaderReflection)) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE reflection_add_ref(ID3D10ShaderReflection *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE reflection_release(ID3D10ShaderReflection *iface)
{
    Reflection *r = impl(iface);
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs)
        destroy(r);
    return refs;
}

static HRESULT STDMETHODCALLTYPE reflection_get_desc(ID3D10ShaderReflection *iface, D3D10_SHADER_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = impl(iface)->desc;
    return S_OK;
}

static ID3D10ShaderReflectionConstantBuffer *STDMETHODCALLTYPE reflection_buffer_by_index(ID3D10ShaderReflection *iface,
                                                                                          UINT index)
{
    Reflection *r = impl(iface);
    return index < r->desc.ConstantBuffers ? &r->buffers[index].iface : &invalid_buffer;
}

static ID3D10ShaderReflectionConstantBuffer *STDMETHODCALLTYPE reflection_buffer_by_name(ID3D10ShaderReflection *iface,
                                                                                         LPCSTR name)
{
    Reflection *r = impl(iface);
    for (UINT i = 0; name && i < r->desc.ConstantBuffers; i++)
        if (!strcmp(r->buffers[i].desc.Name, name))
            return &r->buffers[i].iface;
    return &invalid_buffer;
}

static HRESULT STDMETHODCALLTYPE reflection_binding(ID3D10ShaderReflection *iface, UINT index,
                                                    D3D10_SHADER_INPUT_BIND_DESC *desc)
{
    Reflection *r = impl(iface);
    if (!desc || index >= r->desc.BoundResources)
        return E_INVALIDARG;
    *desc = r->bindings[index];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reflection_input(ID3D10ShaderReflection *iface, UINT index,
                                                  D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    Reflection *r = impl(iface);
    if (!desc || index >= r->desc.InputParameters)
        return E_INVALIDARG;
    *desc = r->inputs[index];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reflection_output(ID3D10ShaderReflection *iface, UINT index,
                                                   D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    Reflection *r = impl(iface);
    if (!desc || index >= r->desc.OutputParameters)
        return E_INVALIDARG;
    *desc = r->outputs[index];
    return S_OK;
}

static const ID3D10ShaderReflectionVtbl reflection_vtbl = {
    reflection_query,          reflection_add_ref, reflection_release, reflection_get_desc, reflection_buffer_by_index,
    reflection_buffer_by_name, reflection_binding, reflection_input,   reflection_output,
};

/* a container that does not read is an invalid argument. */
D3D10API HRESULT WINAPI D3D10ReflectShader(const void *bytecode, SIZE_T length, ID3D10ShaderReflection **out)
{
    if (!out || !bytecode)
        return E_INVALIDARG;
    *out = nullptr;
    Reflection *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->iface.lpVtbl = &reflection_vtbl;
    r->refs = 1;
    if (FAILED(initialize(r, bytecode, length))) {
        destroy(r);
        return E_INVALIDARG;
    }
    *out = &r->iface;
    return S_OK;
}
