/*
 * the effects framework: D3D10CreateEffectFromMemory loads the FX10 chunk
 * of an fx_4_0 container as d3d10.dll's CEffectLoader reads it, then makes
 * the effect's constant buffers, states and shaders on the device. passes
 * apply their states and shaders, and each shader its constant buffers,
 * views and samplers, found by name in the reflection of its bytecode.
 * state assignments fxc could not fold run as expressions,
 * effect_expression.c.
 */
#include "effect_private.h"
#include "../lib/dxbc/dxbc.h"

#define VTBL_FN(f) ((void *)(f))

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }

void *FxAlloc(FxEffect *effect, SIZE_T size)
{
    FxArena *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FxArena) + 8 + size);
    if (!block)
        return NULL;
    block->next = effect->arena;
    effect->arena = block;
    return (BYTE *)block + ((sizeof(FxArena) + 7) & ~7);
}

/* ------------------------------------------------------------------ the state table */

/* the value types of states, direct3d's shader variable types. */
enum { ST_BOOL = 1, ST_INT = 2, ST_FLOAT = 3, ST_UINT = 19, ST_UINT8 = 20, ST_OBJECT = 0 };

typedef struct FxStateInfo {
    UINT block, type, columns, count;
    /* the object a state takes, an object type of the format, or 0 for numbers. */
    UINT object;
    SIZE_T offset;
} FxStateInfo;

#define DS(f) offsetof(D3D10_DEPTH_STENCIL_DESC, f)
#define BL(f) offsetof(D3D10_BLEND_DESC, f)
#define RS(f) offsetof(D3D10_RASTERIZER_DESC, f)
#define SM(f) offsetof(D3D10_SAMPLER_DESC, f)
#define PS(f) offsetof(FxPass, f)

/* the 56 states of d3d10.dll's table, in its order, which assignments name by index. */
static const FxStateInfo STATES[] = {
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_RASTERIZER, PS(rasterizer)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_DEPTHSTENCIL, PS(depth_stencil)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_BLEND, PS(blend)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 8, FX_RENDERTARGETVIEW, PS(rtv)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_DEPTHSTENCILVIEW, PS(dsv)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_TEXTURE, PS(mips)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_VERTEXSHADER, PS(vs)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_PIXELSHADER, PS(ps)},
    {FX_BLOCK_PASS, ST_OBJECT, 1, 1, FX_GEOMETRYSHADER, PS(gs)},
    {FX_BLOCK_PASS, ST_UINT, 1, 1, 0, PS(stencil_ref)},
    {FX_BLOCK_PASS, ST_FLOAT, 4, 1, 0, PS(blend_factor)},
    {FX_BLOCK_PASS, ST_UINT, 1, 1, 0, PS(sample_mask)},
    {FX_BLOCK_RASTERIZER, ST_UINT, 1, 1, 0, RS(FillMode)},
    {FX_BLOCK_RASTERIZER, ST_UINT, 1, 1, 0, RS(CullMode)},
    {FX_BLOCK_RASTERIZER, ST_BOOL, 1, 1, 0, RS(FrontCounterClockwise)},
    {FX_BLOCK_RASTERIZER, ST_UINT, 1, 1, 0, RS(DepthBias)},
    {FX_BLOCK_RASTERIZER, ST_FLOAT, 1, 1, 0, RS(DepthBiasClamp)},
    {FX_BLOCK_RASTERIZER, ST_FLOAT, 1, 1, 0, RS(SlopeScaledDepthBias)},
    {FX_BLOCK_RASTERIZER, ST_BOOL, 1, 1, 0, RS(DepthClipEnable)},
    {FX_BLOCK_RASTERIZER, ST_BOOL, 1, 1, 0, RS(ScissorEnable)},
    {FX_BLOCK_RASTERIZER, ST_BOOL, 1, 1, 0, RS(MultisampleEnable)},
    {FX_BLOCK_RASTERIZER, ST_BOOL, 1, 1, 0, RS(AntialiasedLineEnable)},
    {FX_BLOCK_DEPTHSTENCIL, ST_BOOL, 1, 1, 0, DS(DepthEnable)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(DepthWriteMask)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(DepthFunc)},
    {FX_BLOCK_DEPTHSTENCIL, ST_BOOL, 1, 1, 0, DS(StencilEnable)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT8, 1, 1, 0, DS(StencilReadMask)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT8, 1, 1, 0, DS(StencilWriteMask)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(FrontFace.StencilFailOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(FrontFace.StencilDepthFailOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(FrontFace.StencilPassOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(FrontFace.StencilFunc)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(BackFace.StencilFailOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(BackFace.StencilDepthFailOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(BackFace.StencilPassOp)},
    {FX_BLOCK_DEPTHSTENCIL, ST_UINT, 1, 1, 0, DS(BackFace.StencilFunc)},
    {FX_BLOCK_BLEND, ST_BOOL, 1, 1, 0, BL(AlphaToCoverageEnable)},
    {FX_BLOCK_BLEND, ST_BOOL, 1, 8, 0, BL(BlendEnable)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(SrcBlend)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(DestBlend)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(BlendOp)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(SrcBlendAlpha)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(DestBlendAlpha)},
    {FX_BLOCK_BLEND, ST_UINT, 1, 1, 0, BL(BlendOpAlpha)},
    {FX_BLOCK_BLEND, ST_UINT8, 1, 8, 0, BL(RenderTargetWriteMask)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(Filter)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(AddressU)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(AddressV)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(AddressW)},
    {FX_BLOCK_SAMPLER, ST_FLOAT, 1, 1, 0, SM(MipLODBias)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(MaxAnisotropy)},
    {FX_BLOCK_SAMPLER, ST_UINT, 1, 1, 0, SM(ComparisonFunc)},
    {FX_BLOCK_SAMPLER, ST_FLOAT, 4, 1, 0, SM(BorderColor)},
    {FX_BLOCK_SAMPLER, ST_FLOAT, 1, 1, 0, SM(MinLOD)},
    {FX_BLOCK_SAMPLER, ST_FLOAT, 1, 1, 0, SM(MaxLOD)},
    {FX_BLOCK_SAMPLER, ST_OBJECT, 1, 1, FX_TEXTURE, offsetof(FxBlock, texture)},
};

/* the bytes a value of the state takes: four a component, one for masks. */
static UINT state_size(const FxStateInfo *info) { return info->type == ST_UINT8 ? 1 : 4 * info->columns; }

/* where an object variable of the type of the state is acceptable: shaders and textures by family. */
static BOOL object_fits(UINT state_object, UINT object)
{
    if (state_object == FX_TEXTURE)
        return (object >= FX_TEXTURE && object <= FX_TEXTURECUBE) || object == FX_BUFFER;
    if (state_object == FX_GEOMETRYSHADER)
        return object == FX_GEOMETRYSHADER || object == FX_GEOMETRYSHADER_SO;
    return object == state_object;
}

/* the null objects constant assignments of 0 name. */
static FxBlock null_blocks[5] = {{0}, {FX_BLOCK_DEPTHSTENCIL}, {FX_BLOCK_BLEND}, {FX_BLOCK_RASTERIZER}, {0}};
static FxShader null_shaders[3] = {{FX_PIXELSHADER}, {FX_VERTEXSHADER}, {FX_GEOMETRYSHADER}};
static void *null_view;

static void *null_object(UINT object)
{
    switch (object) {
    case FX_RASTERIZER:
        return &null_blocks[FX_BLOCK_RASTERIZER];
    case FX_DEPTHSTENCIL:
        return &null_blocks[FX_BLOCK_DEPTHSTENCIL];
    case FX_BLEND:
        return &null_blocks[FX_BLOCK_BLEND];
    case FX_PIXELSHADER:
        return &null_shaders[0];
    case FX_VERTEXSHADER:
        return &null_shaders[1];
    case FX_GEOMETRYSHADER:
        return &null_shaders[2];
    default:
        return &null_view;
    }
}

/* ------------------------------------------------------------------ objects of variables */

UINT FxElementCount(const FxType *type) { return type->elements ? type->elements : 1; }

/* the storage of an element of an object variable: a block, a shader, a view pointer or a string. */
static void *object_element(FxVariable *v, UINT index)
{
    switch (v->type->object) {
    case FX_BLEND:
    case FX_DEPTHSTENCIL:
    case FX_RASTERIZER:
    case FX_SAMPLER:
        return (FxBlock *)v->data + index;
    case FX_PIXELSHADER:
    case FX_VERTEXSHADER:
    case FX_GEOMETRYSHADER:
    case FX_GEOMETRYSHADER_SO:
        return (FxShader *)v->data + index;
    default:
        return (void **)v->data + index;
    }
}

/* ------------------------------------------------------------------ reading */

typedef struct FxReader {
    FxEffect *effect;
    const BYTE *base;
    UINT size;
    /* the unstructured data, which offsets of the structured stream point into. */
    const BYTE *data;
    UINT data_size;
    UINT pos;
    UINT header[19];
    UINT inline_shaders;
    struct FxTypeCache {
        UINT offset;
        FxType *type;
    } *types;
    UINT type_count, type_capacity;
} FxReader;

static BOOL next(FxReader *r, UINT *out)
{
    if (r->pos > r->size || r->size - r->pos < 4)
        return FALSE;
    *out = read32(r->base + r->pos);
    r->pos += 4;
    return TRUE;
}

static const BYTE *next_bytes(FxReader *r, UINT size)
{
    if (r->pos > r->size || r->size - r->pos < size)
        return NULL;
    const BYTE *p = r->base + r->pos;
    r->pos += size;
    return p;
}

/* bytes of the unstructured data. */
static const BYTE *data_at(FxReader *r, UINT offset, UINT size)
{
    if (offset > r->data_size || r->data_size - offset < size)
        return NULL;
    return r->data + offset;
}

/* a string of the unstructured data, NULL for offset 0; FALSE when it runs past the data. */
static BOOL string_at(FxReader *r, UINT offset, const char **out)
{
    *out = NULL;
    if (!offset)
        return TRUE;
    if (offset >= r->data_size)
        return FALSE;
    for (UINT i = offset; i < r->data_size; i++)
        if (!r->data[i]) {
            *out = (const char *)r->data + offset;
            return TRUE;
        }
    return FALSE;
}

/* a block of the unstructured data, its size first. */
static const BYTE *block_at(FxReader *r, UINT offset, UINT *size)
{
    const BYTE *p = data_at(r, offset, 4);
    if (!p)
        return NULL;
    *size = read32(p);
    return data_at(r, offset + 4, *size);
}

static HRESULT read_type(FxReader *r, UINT offset, FxType **out)
{
    for (UINT i = 0; i < r->type_count; i++)
        if (r->types[i].offset == offset) {
            *out = r->types[i].type;
            return S_OK;
        }
    const BYTE *p = data_at(r, offset, 24);
    if (!p)
        return E_FAIL;
    FxType *t = FxAlloc(r->effect, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    if (!string_at(r, read32(p), &t->name))
        return E_FAIL;
    t->kind = read32(p + 4);
    t->elements = read32(p + 8);
    t->size = read32(p + 12);
    t->stride = read32(p + 16);
    t->packed = read32(p + 20);
    UINT n = FxElementCount(t);
    if (n * t->stride != ((t->size + 15) & ~15u) || (t->stride & 15) || t->packed > t->size || t->packed % n)
        return E_FAIL;
    const BYTE *more = p + 24;
    UINT pos = offset + 24;
    if (t->kind == FX_NUMERIC) {
        if (!data_at(r, pos, 4))
            return E_FAIL;
        t->numeric = read32(more);
        UINT layout = FX_LAYOUT(t->numeric), base = FX_BASE(t->numeric);
        if (FX_ROWS(t->numeric) < 1 || FX_ROWS(t->numeric) > 4 || FX_COLUMNS(t->numeric) < 1 ||
            FX_COLUMNS(t->numeric) > 4 || !layout || base < 1 || base > 4 ||
            (layout != FX_MATRIX && (t->numeric & FX_COLUMN_MAJOR)))
            return E_FAIL;
    } else if (t->kind == FX_OBJECT) {
        if (!data_at(r, pos, 4))
            return E_FAIL;
        t->object = read32(more);
        if (t->object < 1 || t->object > 22)
            return E_FAIL;
    } else if (t->kind == FX_STRUCT) {
        if (!data_at(r, pos, 4))
            return E_FAIL;
        t->member_count = read32(more);
        const BYTE *m = data_at(r, pos + 4, 0);
        if (!m || (UINT64)t->member_count * 16 > r->data_size - (pos + 4))
            return E_FAIL;
        if (!(t->members = FxAlloc(r->effect, t->member_count * sizeof(*t->members) + 1)))
            return E_OUTOFMEMORY;
        UINT end = 0;
        for (UINT i = 0; i < t->member_count; i++, m += 16) {
            FxMember *member = &t->members[i];
            member->offset = read32(m + 8);
            if (member->offset != end && member->offset != ((end + 15) & ~15u))
                return E_FAIL;
            HRESULT hr = read_type(r, read32(m + 12), &member->type);
            if (FAILED(hr))
                return hr;
            if (!string_at(r, read32(m), &member->name) || !string_at(r, read32(m + 4), &member->semantic))
                return E_FAIL;
            end = member->offset + member->type->size;
        }
        if (((end + 15) & ~15u) != t->stride)
            return E_FAIL;
    } else {
        return E_FAIL;
    }
    FxInitType(t);
    if (r->type_count == r->type_capacity) {
        UINT capacity = r->type_capacity ? 2 * r->type_capacity : 16;
        void *types = r->types ? HeapReAlloc(GetProcessHeap(), 0, r->types, capacity * sizeof(*r->types))
                               : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*r->types));
        if (!types)
            return E_OUTOFMEMORY;
        r->types = types;
        r->type_capacity = capacity;
    }
    r->types[r->type_count].offset = offset;
    r->types[r->type_count++].type = t;
    *out = t;
    return S_OK;
}

/*
 * packed values, the components one after the other, spread to the layout
 * of constant buffers: each row, or column of column major matrices, and
 * each element of an array starting a register of 16 bytes.
 */
static BOOL unpack(BYTE *out, const BYTE *in, UINT size, const FxType *t, UINT *used)
{
    UINT n = FxElementCount(t), pos = 0;
    if (t->kind == FX_NUMERIC) {
        UINT rows = FX_ROWS(t->numeric), cols = FX_COLUMNS(t->numeric);
        BOOL column_major = (t->numeric & FX_COLUMN_MAJOR) != 0;
        for (UINT e = 0; e < n; e++) {
            if (size - pos < 4 * rows * cols)
                return FALSE;
            BYTE *element = out + e * t->stride;
            if (column_major) {
                for (UINT c = 0; c < cols; c++)
                    for (UINT row = 0; row < rows; row++)
                        memcpy(element + 16 * c + 4 * row, in + pos + 4 * (row * cols + c), 4);
            } else {
                for (UINT row = 0; row < rows; row++)
                    memcpy(element + 16 * row, in + pos + 4 * row * cols, 4 * cols);
            }
            pos += 4 * rows * cols;
        }
    } else if (t->kind == FX_STRUCT) {
        for (UINT e = 0; e < n; e++)
            for (UINT i = 0; i < t->member_count; i++) {
                UINT member_used;
                if (!unpack(out + e * t->stride + t->members[i].offset, in + pos, size - pos, t->members[i].type,
                            &member_used))
                    return FALSE;
                pos += member_used;
            }
    } else {
        return FALSE;
    }
    *used = pos;
    return TRUE;
}

/* a numeric variable's default value, or an annotation's. */
static HRESULT read_value(FxReader *r, UINT offset, const FxType *t, BYTE *out)
{
    const BYTE *in = data_at(r, offset, t->packed);
    UINT used;
    if (!in || !unpack(out, in, t->packed, t, &used) || used != t->packed)
        return E_FAIL;
    return S_OK;
}

static HRESULT read_annotations(FxReader *r, UINT *count, FxVariable **out)
{
    if (!next(r, count))
        return E_FAIL;
    if (!*count)
        return S_OK;
    FxVariable *a = FxAlloc(r->effect, *count * sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    *out = a;
    for (UINT i = 0; i < *count; i++) {
        UINT name, type_offset;
        if (!next(r, &name) || !next(r, &type_offset))
            return E_FAIL;
        HRESULT hr = read_type(r, type_offset, &a[i].type);
        if (FAILED(hr))
            return hr;
        if (!string_at(r, name, &a[i].name))
            return E_FAIL;
        a[i].effect = r->effect;
        a[i].annotation = TRUE;
        a[i].top = &a[i];
        a[i].bind_point = ~0u;
        FxType *t = a[i].type;
        if (t->kind == FX_OBJECT) {
            UINT n = FxElementCount(t);
            if (t->object != FX_STRING || !(a[i].data = FxAlloc(r->effect, n * sizeof(char *))))
                return t->object != FX_STRING ? E_FAIL : E_OUTOFMEMORY;
            for (UINT e = 0; e < n; e++) {
                UINT s;
                if (!next(r, &s) || !string_at(r, s, &((const char **)a[i].data)[e]))
                    return E_FAIL;
            }
        } else {
            UINT value;
            if (!next(r, &value) || !value)
                return E_FAIL;
            if (!(a[i].data = FxAlloc(r->effect, t->size)))
                return E_OUTOFMEMORY;
            if (FAILED(hr = read_value(r, value, t, a[i].data)))
                return hr;
        }
        FxInitVariable(&a[i]);
    }
    return S_OK;
}

static FxBuffer *find_buffer(FxEffect *effect, const char *name)
{
    for (; effect; effect = effect->pool)
        for (UINT i = 0; i < effect->buffer_count; i++)
            if (effect->buffers[i].var.name && !strcmp(effect->buffers[i].var.name, name))
                return &effect->buffers[i];
    return NULL;
}

FxVariable *FxFindVariable(FxEffect *effect, const char *name)
{
    for (; effect; effect = effect->pool)
        for (UINT i = 0; i < effect->variable_count; i++)
            if (effect->variables[i].name && !strcmp(effect->variables[i].name, name))
                return &effect->variables[i];
    return NULL;
}

/* a name a child effect gives may not be one of its pool's. */
static BOOL pooled_name(FxReader *r, const char *name)
{
    return name && r->effect->pool && (find_buffer(r->effect->pool, name) || FxFindVariable(r->effect->pool, name));
}

static HRESULT read_buffers(FxReader *r)
{
    FxEffect *e = r->effect;
    UINT count = r->header[1];
    if (count && !(e->buffers = FxAlloc(e, count * sizeof(*e->buffers))))
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        FxBuffer *b = &e->buffers[i];
        const BYTE *p = next_bytes(r, 20);
        if (!p || !string_at(r, read32(p), &b->var.name) || pooled_name(r, b->var.name))
            return E_FAIL;
        b->size = read32(p + 4);
        b->tbuffer = read32(p + 8) & 1;
        b->variable_count = read32(p + 12);
        b->var.bind_point = read32(p + 16);
        if (b->size & 15)
            return E_FAIL;
        if (!(b->store = FxAlloc(e, b->size)))
            return E_OUTOFMEMORY;
        b->var.effect = e;
        b->variables = e->variables + e->variable_count;
        HRESULT hr = read_annotations(r, &b->var.annotation_count, &b->var.annotations);
        if (FAILED(hr))
            return hr;
        for (UINT j = 0; j < b->variable_count; j++) {
            if (e->variable_count >= r->header[2] + r->header[3])
                return E_FAIL;
            FxVariable *v = &e->variables[e->variable_count];
            p = next_bytes(r, 24);
            if (!p || FAILED(hr = read_type(r, read32(p + 4), &v->type)))
                return p ? hr : E_FAIL;
            UINT offset = read32(p + 12);
            if (!string_at(r, read32(p), &v->name) || !string_at(r, read32(p + 8), &v->semantic) ||
                pooled_name(r, v->name) || offset >= b->size || v->type->size > b->size - offset)
                return E_FAIL;
            v->effect = e;
            v->buffer = b;
            v->data = b->store + offset;
            v->top = v;
            v->bind_point = read32(p + 20) & 4 ? offset : ~0u;
            if (read32(p + 16) && FAILED(hr = read_value(r, read32(p + 16), v->type, v->data)))
                return hr;
            if (FAILED(hr = read_annotations(r, &v->annotation_count, &v->annotations)))
                return hr;
            FxInitVariable(v);
            e->variable_count++;
        }
        FxInitBuffer(b);
    }
    e->buffer_count = count;
    return S_OK;
}

/* a shader of a variable or an inline one, its bytecode in the unstructured data. */
static HRESULT read_shader(FxReader *r, FxShader *s, UINT kind, UINT code_offset, UINT so_offset)
{
    s->kind = kind;
    if (code_offset) {
        UINT size;
        const BYTE *code = block_at(r, code_offset, &size);
        if (!code)
            return E_FAIL;
        if (size) {
            s->code = code;
            s->length = size;
        }
    }
    if (so_offset && s->code && !string_at(r, so_offset, &s->so_decl))
        return E_FAIL;
    return S_OK;
}

static FxShader *new_shader(FxReader *r)
{
    FxEffect *e = r->effect;
    return e->shader_count < e->shader_capacity ? &e->shaders[e->shader_count++] : NULL;
}

static FxBlock *new_block(FxReader *r, UINT kind)
{
    FxEffect *e = r->effect;
    if (e->block_count >= e->block_capacity)
        return NULL;
    FxBlock *b = &e->blocks[e->block_count++];
    b->kind = kind;
    /* the defaults of d3d10.dll's blocks, before their assignments. */
    switch (kind) {
    case FX_BLOCK_DEPTHSTENCIL:
        b->desc.depth_stencil = (D3D10_DEPTH_STENCIL_DESC){
            TRUE,
            D3D10_DEPTH_WRITE_MASK_ALL,
            D3D10_COMPARISON_LESS,
            FALSE,
            0xFF,
            0xFF,
            {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS},
            {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS}};
        break;
    case FX_BLOCK_BLEND:
        b->desc.blend.SrcBlend = b->desc.blend.SrcBlendAlpha = D3D10_BLEND_SRC_ALPHA;
        b->desc.blend.DestBlend = b->desc.blend.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        b->desc.blend.BlendOp = b->desc.blend.BlendOpAlpha = D3D10_BLEND_OP_ADD;
        memset(b->desc.blend.RenderTargetWriteMask, D3D10_COLOR_WRITE_ENABLE_ALL, 8);
        break;
    case FX_BLOCK_RASTERIZER:
        b->desc.rasterizer.FillMode = D3D10_FILL_SOLID;
        b->desc.rasterizer.CullMode = D3D10_CULL_BACK;
        b->desc.rasterizer.DepthClipEnable = TRUE;
        break;
    case FX_BLOCK_SAMPLER:
        b->desc.sampler.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
        b->desc.sampler.AddressU = b->desc.sampler.AddressV = b->desc.sampler.AddressW = D3D10_TEXTURE_ADDRESS_WRAP;
        b->desc.sampler.MaxAnisotropy = 16;
        b->desc.sampler.ComparisonFunc = D3D10_COMPARISON_NEVER;
        b->desc.sampler.MaxLOD = 3.402823466e+38f;
        break;
    }
    return b;
}

static void evaluate(FxAssignment *a);

/* a constant of an assignment as the state's type takes it: 1 a float, 2 to 4 integers. */
static BOOL convert_constant(UINT from, const BYTE *value, UINT to, BYTE *out)
{
    INT i;
    float f;
    memcpy(&i, value, 4);
    memcpy(&f, value, 4);
    if (from == 1) {
        if (to == ST_FLOAT)
            memcpy(out, &f, 4);
        else if (to == ST_UINT8)
            *out = (BYTE)(INT)f;
        else
            i = (INT)f, memcpy(out, &i, 4);
        return TRUE;
    }
    if (from < 2 || from > 4)
        return FALSE;
    if (to == ST_FLOAT)
        f = (float)i, memcpy(out, &f, 4);
    else if (to == ST_UINT8)
        *out = (BYTE)i;
    else
        memcpy(out, &i, 4);
    return TRUE;
}

/*
 * the assignments of a block or pass: constants go to their state at once,
 * variables are kept for each apply but objects, held by where they are.
 */
static HRESULT read_assignments(FxReader *r, UINT count, UINT kind, void *block, UINT *assignment_count,
                                FxAssignment **out, UINT *rtv_count)
{
    FxEffect *e = r->effect;
    const BYTE *records = next_bytes(r, 16 * count);
    if (!records)
        return E_FAIL;
    FxAssignment *list = count ? FxAlloc(e, count * sizeof(*list)) : NULL;
    if (count && !list)
        return E_OUTOFMEMORY;
    UINT kept = 0;
    for (UINT i = 0; i < count; i++) {
        const BYTE *p = records + 16 * i;
        UINT state = read32(p), index = read32(p + 4), how = read32(p + 8), offset = read32(p + 12);
        if (state >= RTL_NUMBER_OF(STATES) || index >= STATES[state].count || STATES[state].block != kind)
            return E_FAIL;
        const FxStateInfo *info = &STATES[state];
        BYTE *base = kind == FX_BLOCK_PASS || info->object ? (BYTE *)block : (BYTE *)&((FxBlock *)block)->desc;
        BYTE *target = base + info->offset + (info->object ? sizeof(void *) : state_size(info)) * index;
        if (state == 3)
            (*rtv_count)++;
        FxAssignment a = {state, index, how, target};
        switch (how) {
        case 1: {
            const BYTE *c = data_at(r, offset, 4);
            UINT n = c ? read32(c) : 0;
            if (!c || !(c = data_at(r, offset + 4, 8 * n)))
                return E_FAIL;
            if (info->object) {
                if (n != 1 || read32(c) < 2 || read32(c) > 3 || read32(c + 4))
                    return E_FAIL;
                *(void **)target = null_object(info->object);
                break;
            }
            if (n != info->columns)
                return E_FAIL;
            for (UINT k = 0; k < n; k++)
                if (!convert_constant(read32(c + 8 * k), c + 8 * k + 4, info->type,
                                      target + (info->type == ST_UINT8 ? 1 : 4) * k))
                    return E_FAIL;
            break;
        }
        case 2:
        case 3: {
            const char *name;
            UINT element = 0;
            if (how == 3) {
                const BYTE *ci = data_at(r, offset, 8);
                if (!ci)
                    return E_FAIL;
                offset = read32(ci);
                element = read32(ci + 4);
            }
            if (!string_at(r, offset, &name) || !name || !(a.source = FxFindVariable(e, name)))
                return E_FAIL;
            FxType *t = a.source->type;
            if (info->object) {
                if (t->kind != FX_OBJECT || !object_fits(info->object, t->object) || element >= FxElementCount(t))
                    return E_FAIL;
                *(void **)target = object_element(a.source, element);
                break;
            }
            if (t->kind == FX_OBJECT || 4 * element + state_size(info) > t->size)
                return E_FAIL;
            a.constant_index = element;
            list[kept++] = a;
            break;
        }
        case 4: {
            const BYTE *vi = data_at(r, offset, 8);
            const char *array, *index_name;
            if (!vi || !string_at(r, read32(vi), &array) || !string_at(r, read32(vi + 4), &index_name) || !array ||
                !index_name || !(a.source = FxFindVariable(e, array)) ||
                !(a.index_source = FxFindVariable(e, index_name)))
                return E_FAIL;
            FxType *it = a.index_source->type;
            if (it->kind != FX_NUMERIC || FX_BASE(it->numeric) < FX_INT || FX_BASE(it->numeric) > FX_UINT ||
                !a.source->type->elements)
                return E_FAIL;
            if (info->object ? a.source->type->kind != FX_OBJECT || !object_fits(info->object, a.source->type->object)
                             : a.source->type->kind == FX_OBJECT)
                return E_FAIL;
            if (info->object)
                *(void **)target = object_element(a.source, 0);
            list[kept++] = a;
            break;
        }
        case 5: {
            /* an element of an array, its index computed. */
            const BYTE *xi = data_at(r, offset, 8), *blob;
            const char *array;
            UINT size;
            HRESULT hr;
            if (!xi || !string_at(r, read32(xi), &array) || !array || !(a.source = FxFindVariable(e, array)) ||
                !(blob = block_at(r, read32(xi + 4), &size)))
                return E_FAIL;
            if (info->object ? a.source->type->kind != FX_OBJECT || !object_fits(info->object, a.source->type->object)
                             : a.source->type->kind == FX_OBJECT || !a.source->type->elements)
                return E_FAIL;
            if (FAILED(hr = FxParseExpression(e, blob, size, &a.expression)))
                return hr;
            evaluate(&a);
            list[kept++] = a;
            break;
        }
        case 6: {
            /* a value computed. */
            const BYTE *blob;
            UINT size;
            HRESULT hr;
            if (info->object || !(blob = block_at(r, offset, &size)))
                return E_FAIL;
            if (FAILED(hr = FxParseExpression(e, blob, size, &a.expression)))
                return hr;
            evaluate(&a);
            list[kept++] = a;
            break;
        }
        case 7: {
            const BYTE *si = data_at(r, offset, 8);
            UINT shader_kind = state == 6 ? FX_VERTEXSHADER : state == 7 ? FX_PIXELSHADER : FX_GEOMETRYSHADER;
            FxShader *s =
                si && info->object >= FX_PIXELSHADER && info->object <= FX_GEOMETRYSHADER ? new_shader(r) : NULL;
            if (!s || ++r->inline_shaders > r->header[18])
                return E_FAIL;
            HRESULT hr = read_shader(r, s, shader_kind, read32(si), read32(si + 4));
            if (FAILED(hr))
                return hr;
            if (shader_kind != FX_GEOMETRYSHADER && s->so_decl)
                return E_FAIL;
            *(void **)target = s;
            break;
        }
        default:
            return E_FAIL;
        }
    }
    *assignment_count = kept;
    *out = list;
    return S_OK;
}

static HRESULT read_objects(FxReader *r)
{
    FxEffect *e = r->effect;
    for (UINT i = 0; i < r->header[3]; i++) {
        if (e->variable_count >= r->header[2] + r->header[3])
            return E_FAIL;
        FxVariable *v = &e->variables[e->variable_count];
        const BYTE *p = next_bytes(r, 16);
        HRESULT hr;
        if (!p || FAILED(hr = read_type(r, read32(p + 4), &v->type)))
            return p ? hr : E_FAIL;
        if (!string_at(r, read32(p), &v->name) || !string_at(r, read32(p + 8), &v->semantic) ||
            pooled_name(r, v->name) || v->type->kind != FX_OBJECT)
            return E_FAIL;
        v->effect = e;
        v->top = v;
        v->bind_point = read32(p + 12);
        e->variable_count++;
        FxType *t = v->type;
        UINT n = FxElementCount(t);
        switch (t->object) {
        case FX_BLEND:
        case FX_DEPTHSTENCIL:
        case FX_RASTERIZER:
        case FX_SAMPLER: {
            UINT kind = t->object == FX_BLEND          ? FX_BLOCK_BLEND
                        : t->object == FX_DEPTHSTENCIL ? FX_BLOCK_DEPTHSTENCIL
                        : t->object == FX_RASTERIZER   ? FX_BLOCK_RASTERIZER
                                                       : FX_BLOCK_SAMPLER;
            for (UINT k = 0; k < n; k++) {
                FxBlock *b = new_block(r, kind);
                UINT count, rtvs = 0;
                if (!b || !next(r, &count))
                    return E_FAIL;
                if (!k)
                    v->data = (BYTE *)b;
                if (FAILED(hr = read_assignments(r, count, kind, b, &b->assignment_count, &b->assignments, &rtvs)))
                    return hr;
            }
            break;
        }
        case FX_PIXELSHADER:
        case FX_VERTEXSHADER:
        case FX_GEOMETRYSHADER:
        case FX_GEOMETRYSHADER_SO:
            for (UINT k = 0; k < n; k++) {
                FxShader *s = new_shader(r);
                UINT code, so = 0;
                if (!s || !next(r, &code) || (t->object == FX_GEOMETRYSHADER_SO && !next(r, &so)))
                    return E_FAIL;
                if (!k)
                    v->data = (BYTE *)s;
                UINT kind = t->object == FX_GEOMETRYSHADER_SO ? FX_GEOMETRYSHADER : t->object;
                if (FAILED(hr = read_shader(r, s, kind, code, so)))
                    return hr;
                s->variable = v;
                s->variable_index = k;
            }
            break;
        case FX_STRING:
            if (!(v->data = FxAlloc(e, n * sizeof(char *))))
                return E_OUTOFMEMORY;
            for (UINT k = 0; k < n; k++) {
                UINT s;
                if (!next(r, &s) || !string_at(r, s, &((const char **)v->data)[k]))
                    return E_FAIL;
            }
            break;
        case FX_RENDERTARGETVIEW:
        case FX_DEPTHSTENCILVIEW:
        case FX_BUFFER:
        default:
            if (t->object == FX_CONSTANTBUFFER || (t->object < FX_TEXTURE && t->object != FX_STRING))
                return E_FAIL;
            if (!(v->data = FxAlloc(e, n * sizeof(void *))))
                return E_OUTOFMEMORY;
            break;
        }
        if (FAILED(hr = read_annotations(r, &v->annotation_count, &v->annotations)))
            return hr;
        FxInitVariable(v);
    }
    return S_OK;
}

/* what a child effect shares with its pool must be there, the same. */
static HRESULT read_shared(FxReader *r)
{
    for (UINT i = 0; i < r->header[4]; i++) {
        const BYTE *p = next_bytes(r, 20);
        const char *name;
        FxBuffer *b;
        if (!p || !string_at(r, read32(p), &name) || !name || !r->effect->pool ||
            !(b = find_buffer(r->effect->pool, name)) || b->variable_count != read32(p + 12) ||
            b->tbuffer != (read32(p + 8) & 1))
            return E_FAIL;
        for (UINT j = 0; j < b->variable_count; j++) {
            const BYTE *v = next_bytes(r, 24);
            if (!v || !string_at(r, read32(v), &name) || !name || strcmp(name, b->variables[j].name) ||
                (UINT)(b->variables[j].data - b->store) != read32(v + 12))
                return E_FAIL;
        }
    }
    for (UINT i = 0; i < r->header[6]; i++) {
        const BYTE *p = next_bytes(r, 16);
        const char *name;
        if (!p || !string_at(r, read32(p), &name) || !name || !r->effect->pool ||
            !FxFindVariable(r->effect->pool, name))
            return E_FAIL;
    }
    return S_OK;
}

static const ID3D10EffectTechniqueVtbl technique_vtbl;
static const ID3D10EffectPassVtbl pass_vtbl;

static HRESULT read_techniques(FxReader *r)
{
    FxEffect *e = r->effect;
    UINT count = r->header[7];
    if (count && !(e->techniques = FxAlloc(e, count * sizeof(*e->techniques))))
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; i++) {
        FxTechnique *t = &e->techniques[i];
        UINT name;
        HRESULT hr;
        t->iface.lpVtbl = &technique_vtbl;
        t->effect = e;
        if (!next(r, &name) || !next(r, &t->pass_count) || !string_at(r, name, &t->name) || pooled_name(r, t->name))
            return E_FAIL;
        if (t->pass_count && !(t->passes = FxAlloc(e, t->pass_count * sizeof(*t->passes))))
            return E_OUTOFMEMORY;
        if (FAILED(hr = read_annotations(r, &t->annotation_count, &t->annotations)))
            return hr;
        for (UINT j = 0; j < t->pass_count; j++) {
            FxPass *pass = &t->passes[j];
            UINT assignments;
            pass->iface.lpVtbl = &pass_vtbl;
            pass->effect = e;
            if (!next(r, &name) || !next(r, &assignments) || !string_at(r, name, &pass->name))
                return E_FAIL;
            if (FAILED(hr = read_annotations(r, &pass->annotation_count, &pass->annotations)) ||
                FAILED(hr = read_assignments(r, assignments, FX_BLOCK_PASS, pass, &pass->assignment_count,
                                             &pass->assignments, &pass->rtv_count)))
                return hr;
            if (pass->rtv_count > D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT)
                return E_FAIL;
        }
    }
    e->technique_count = count;
    return S_OK;
}

/* ------------------------------------------------------------------ shaders and the device */

/* the resources of a shader, found by name among the effect's buffers and variables. */
static HRESULT bind_shader(FxEffect *e, FxShader *s)
{
    ID3D10ShaderReflection *reflection;
    if (!s->code)
        return S_OK;
    if (FAILED(D3D10ReflectShader(s->code, s->length, &reflection)))
        return E_FAIL;
    D3D10_SHADER_DESC desc;
    reflection->lpVtbl->GetDesc(reflection, &desc);
    HRESULT hr = S_OK;
    if (desc.BoundResources && !(s->bindings = FxAlloc(e, desc.BoundResources * sizeof(*s->bindings))))
        hr = E_OUTOFMEMORY;
    for (UINT i = 0; SUCCEEDED(hr) && i < desc.BoundResources; i++) {
        D3D10_SHADER_INPUT_BIND_DESC bind;
        reflection->lpVtbl->GetResourceBindingDesc(reflection, i, &bind);
        FxBinding *b = &s->bindings[s->binding_count];
        b->slot = bind.BindPoint;
        b->count = bind.BindCount;
        FxVariable *v;
        switch (bind.Type) {
        case D3D_SIT_CBUFFER:
        case D3D_SIT_TBUFFER:
            b->kind = bind.Type == D3D_SIT_CBUFFER ? FX_BIND_CB : FX_BIND_TBUFFER;
            if (!(b->buffer = find_buffer(e, bind.Name)) || b->count != 1 ||
                b->buffer->tbuffer != (bind.Type == D3D_SIT_TBUFFER))
                hr = E_FAIL;
            break;
        case D3D_SIT_TEXTURE:
        case D3D_SIT_SAMPLER:
            b->kind = bind.Type == D3D_SIT_TEXTURE ? FX_BIND_SRV : FX_BIND_SAMPLER;
            v = FxFindVariable(e, bind.Name);
            if (!v || v->type->kind != FX_OBJECT || b->count > FxElementCount(v->type) ||
                (bind.Type == D3D_SIT_SAMPLER ? v->type->object != FX_SAMPLER
                                              : !object_fits(FX_TEXTURE, v->type->object)))
                hr = E_FAIL;
            b->variable = v;
            break;
        default:
            hr = E_FAIL;
            break;
        }
        s->binding_count++;
    }
    reflection->lpVtbl->Release(reflection);
    if (SUCCEEDED(hr) && s->kind != FX_PIXELSHADER)
        hr = D3D10GetInputSignatureBlob(s->code, s->length, &s->signature);
    return hr;
}

/* a declaration of stream output, "SEMANTIC[index][.mask]" entries split by ";", "$SKIP" a gap. */
static UINT parse_so(const char *text, D3D10_SO_DECLARATION_ENTRY *out, char *names, UINT max)
{
    UINT count = 0;
    while (*text && count < max) {
        while (*text == ' ' || *text == '\t' || *text == ';')
            text++;
        if (!*text)
            break;
        D3D10_SO_DECLARATION_ENTRY *entry = &out[count];
        memset(entry, 0, sizeof(*entry));
        if (text[0] >= '0' && text[0] <= '9' && text[1] == ':') {
            entry->OutputSlot = (BYTE)(text[0] - '0');
            text += 2;
        }
        char *name = names + 64 * count;
        UINT n = 0;
        while (*text && *text != '.' && *text != ';' && *text != ' ' && n < 63)
            name[n++] = *text++;
        name[n] = 0;
        UINT index = 0, scale = 1;
        while (n && name[n - 1] >= '0' && name[n - 1] <= '9') {
            index += (name[--n] - '0') * scale;
            scale *= 10;
            name[n] = 0;
        }
        entry->SemanticName = strcmp(name, "$SKIP") ? name : NULL;
        entry->SemanticIndex = index;
        entry->ComponentCount = 4;
        if (*text == '.') {
            static const char XYZW[] = "xyzw", RGBA[] = "rgba";
            UINT first = 4, last = 0;
            for (text++; *text && *text != ';' && *text != ' '; text++)
                for (UINT c = 0; c < 4; c++)
                    if (*text == XYZW[c] || *text == RGBA[c]) {
                        first = first < c ? first : c;
                        last = last > c ? last : c;
                    }
            if (first <= last) {
                entry->StartComponent = (BYTE)first;
                entry->ComponentCount = (BYTE)(last - first + 1);
            }
        }
        while (*text && *text != ';')
            text++;
        count++;
    }
    return count;
}

static HRESULT create_shader(FxEffect *e, FxShader *s)
{
    ID3D10Device *d = e->device;
    if (!s->code)
        return S_OK;
    switch (s->kind) {
    case FX_VERTEXSHADER:
        return d->lpVtbl->CreateVertexShader(d, s->code, s->length, (ID3D10VertexShader **)&s->object);
    case FX_PIXELSHADER:
        return d->lpVtbl->CreatePixelShader(d, s->code, s->length, (ID3D10PixelShader **)&s->object);
    default:
        break;
    }
    if (!s->so_decl)
        return d->lpVtbl->CreateGeometryShader(d, s->code, s->length, (ID3D10GeometryShader **)&s->object);
    D3D10_SO_DECLARATION_ENTRY entries[64];
    static char names[64 * 64];
    UINT count = parse_so(s->so_decl, entries, names, 64), stride = 0;
    for (UINT i = 0; i < count; i++)
        stride = entries[i].OutputSlot || stride == ~0u ? ~0u : stride + 4 * entries[i].ComponentCount;
    return d->lpVtbl->CreateGeometryShaderWithStreamOutput(
        d, s->code, s->length, entries, count, stride == ~0u ? 0 : stride, (ID3D10GeometryShader **)&s->object);
}

static HRESULT create_state(FxEffect *e, FxBlock *b)
{
    ID3D10Device *d = e->device;
    if (b->state)
        b->state->lpVtbl->Release(b->state);
    b->state = NULL;
    switch (b->kind) {
    case FX_BLOCK_DEPTHSTENCIL:
        return d->lpVtbl->CreateDepthStencilState(d, &b->desc.depth_stencil, (ID3D10DepthStencilState **)&b->state);
    case FX_BLOCK_BLEND:
        return d->lpVtbl->CreateBlendState(d, &b->desc.blend, (ID3D10BlendState **)&b->state);
    case FX_BLOCK_RASTERIZER:
        return d->lpVtbl->CreateRasterizerState(d, &b->desc.rasterizer, (ID3D10RasterizerState **)&b->state);
    default:
        return d->lpVtbl->CreateSamplerState(d, &b->desc.sampler, (ID3D10SamplerState **)&b->state);
    }
}

/* the constant buffers, at first written in full, the state objects and the shaders. */
static HRESULT bind_to_device(FxEffect *e)
{
    ID3D10Device *d = e->device;
    HRESULT hr = S_OK;
    for (UINT i = 0; SUCCEEDED(hr) && i < e->buffer_count; i++) {
        FxBuffer *b = &e->buffers[i];
        if (!b->size)
            continue;
        D3D10_BUFFER_DESC desc = {b->size, D3D10_USAGE_DEFAULT,
                                  b->tbuffer ? D3D10_BIND_SHADER_RESOURCE : D3D10_BIND_CONSTANT_BUFFER};
        hr = d->lpVtbl->CreateBuffer(d, &desc, NULL, &b->buffer);
        if (SUCCEEDED(hr) && b->tbuffer) {
            D3D10_SHADER_RESOURCE_VIEW_DESC view = {DXGI_FORMAT_R32G32B32A32_UINT, D3D10_SRV_DIMENSION_BUFFER};
            view.Buffer.ElementWidth = b->size / 16;
            hr = d->lpVtbl->CreateShaderResourceView(d, (ID3D10Resource *)b->buffer, &view, &b->view);
        }
        b->dirty = TRUE;
    }
    for (UINT i = 0; SUCCEEDED(hr) && i < e->block_count; i++)
        hr = create_state(e, &e->blocks[i]);
    for (UINT i = 0; SUCCEEDED(hr) && i < e->shader_count; i++)
        if (SUCCEEDED(hr = bind_shader(e, &e->shaders[i])))
            hr = create_shader(e, &e->shaders[i]);
    return hr;
}

/* ------------------------------------------------------------------ applying */

/*
 * a variable's value or an expression's as a state reads it: an index
 * past the array's elements takes the first, a byte the result's first.
 */
static void evaluate(FxAssignment *a)
{
    const FxStateInfo *info = &STATES[a->state];
    if (a->kind == 6) {
        UINT out[4] = {0};
        memcpy(out, a->target, state_size(info));
        FxRunExpression(a->expression, out);
        memcpy(a->target, out, state_size(info));
        return;
    }
    if (a->kind == 4 || a->kind == 5) {
        UINT out[4] = {0}, index;
        if (a->kind == 4)
            index = *(const UINT *)a->index_source->data;
        else
            FxRunExpression(a->expression, out), index = out[0];
        if (index >= FxElementCount(a->source->type))
            index = 0;
        if (info->object)
            *(void **)a->target = object_element(a->source, index);
        else
            memcpy(a->target, a->source->data + index * a->source->type->stride, state_size(info));
        return;
    }
    memcpy(a->target, a->source->data + 4 * a->constant_index, state_size(info));
}

/* a block's assignments of variables, its state made again when they change it. */
void FxEvaluateBlock(FxEffect *effect, FxBlock *b)
{
    if (!b->assignment_count)
        return;
    BYTE before[sizeof(b->desc)];
    memcpy(before, &b->desc, sizeof(b->desc));
    for (UINT i = 0; i < b->assignment_count; i++)
        evaluate(&b->assignments[i]);
    if (memcmp(before, &b->desc, sizeof(b->desc)) && effect->device)
        create_state(effect, b);
}

typedef void(STDMETHODCALLTYPE *SetBuffers)(ID3D10Device *, UINT, UINT, ID3D10Buffer *const *);
typedef void(STDMETHODCALLTYPE *SetViews)(ID3D10Device *, UINT, UINT, ID3D10ShaderResourceView *const *);
typedef void(STDMETHODCALLTYPE *SetSamplers)(ID3D10Device *, UINT, UINT, ID3D10SamplerState *const *);

/* a constant buffer's bytes, written when they changed and the program did not put its own buffer. */
static void upload(FxEffect *e, FxBuffer *b)
{
    if (!b->dirty || b->user || !b->buffer)
        return;
    e->device->lpVtbl->UpdateSubresource(e->device, (ID3D10Resource *)b->buffer, 0, NULL, b->store, b->size, b->size);
    b->dirty = FALSE;
}

static void apply_shader(FxEffect *e, FxShader *s)
{
    ID3D10Device *d = e->device;
    const ID3D10DeviceVtbl *vt = d->lpVtbl;
    SetBuffers set_buffers = s->kind == FX_VERTEXSHADER  ? vt->VSSetConstantBuffers
                             : s->kind == FX_PIXELSHADER ? vt->PSSetConstantBuffers
                                                         : vt->GSSetConstantBuffers;
    SetViews set_views = s->kind == FX_VERTEXSHADER  ? vt->VSSetShaderResources
                         : s->kind == FX_PIXELSHADER ? vt->PSSetShaderResources
                                                     : vt->GSSetShaderResources;
    SetSamplers set_samplers = s->kind == FX_VERTEXSHADER  ? vt->VSSetSamplers
                               : s->kind == FX_PIXELSHADER ? vt->PSSetSamplers
                                                           : vt->GSSetSamplers;
    for (UINT i = 0; i < s->binding_count; i++) {
        FxBinding *b = &s->bindings[i];
        if (b->kind == FX_BIND_CB) {
            upload(e, b->buffer);
            set_buffers(d, b->slot, 1, &b->buffer->buffer);
        } else if (b->kind == FX_BIND_TBUFFER) {
            upload(e, b->buffer);
            set_views(d, b->slot, 1, &b->buffer->view);
        } else if (b->kind == FX_BIND_SRV) {
            set_views(d, b->slot, b->count, (ID3D10ShaderResourceView *const *)b->variable->data);
        } else {
            ID3D10SamplerState *samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
            UINT count = b->count < RTL_NUMBER_OF(samplers) ? b->count : RTL_NUMBER_OF(samplers);
            for (UINT k = 0; k < count; k++) {
                FxBlock *block = (FxBlock *)b->variable->data + k;
                FxEvaluateBlock(e, block);
                samplers[k] = (ID3D10SamplerState *)block->state;
            }
            set_samplers(d, b->slot, count, samplers);
        }
    }
    if (s->kind == FX_VERTEXSHADER)
        vt->VSSetShader(d, (ID3D10VertexShader *)s->object);
    else if (s->kind == FX_PIXELSHADER)
        vt->PSSetShader(d, (ID3D10PixelShader *)s->object);
    else
        vt->GSSetShader(d, (ID3D10GeometryShader *)s->object);
}

static void evaluate_pass(FxPass *p)
{
    for (UINT i = 0; i < p->assignment_count; i++)
        evaluate(&p->assignments[i]);
}

/* the pass's states, then render targets, then its vertex, pixel and geometry shaders, each as given. */
static void apply_pass(FxPass *p)
{
    FxEffect *e = p->effect;
    ID3D10Device *d = e->device;
    evaluate_pass(p);
    if (p->blend) {
        FxEvaluateBlock(e, p->blend);
        d->lpVtbl->OMSetBlendState(d, (ID3D10BlendState *)p->blend->state, p->blend_factor, p->sample_mask);
    }
    if (p->depth_stencil) {
        FxEvaluateBlock(e, p->depth_stencil);
        d->lpVtbl->OMSetDepthStencilState(d, (ID3D10DepthStencilState *)p->depth_stencil->state, p->stencil_ref);
    }
    if (p->rasterizer) {
        FxEvaluateBlock(e, p->rasterizer);
        d->lpVtbl->RSSetState(d, (ID3D10RasterizerState *)p->rasterizer->state);
    }
    if (p->rtv_count && p->rtv[0]) {
        ID3D10RenderTargetView *rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT] = {0};
        for (UINT i = 0; i < p->rtv_count; i++)
            rtv[i] = p->rtv[i] ? *p->rtv[i] : NULL;
        d->lpVtbl->OMSetRenderTargets(d, p->rtv_count, rtv, p->dsv ? *p->dsv : NULL);
    }
    if (p->vs)
        apply_shader(e, p->vs);
    if (p->ps)
        apply_shader(e, p->ps);
    if (p->gs)
        apply_shader(e, p->gs);
}

/* ------------------------------------------------------------------ passes */

static FxPass *pass_impl(ID3D10EffectPass *iface) { return CONTAINING_RECORD(iface, FxPass, iface); }

static BOOL STDMETHODCALLTYPE pass_is_valid(ID3D10EffectPass *iface) { return TRUE; }

static HRESULT STDMETHODCALLTYPE pass_get_desc(ID3D10EffectPass *iface, D3D10_PASS_DESC *desc)
{
    FxPass *p = pass_impl(iface);
    if (!desc)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Name = p->name;
    desc->Annotations = p->annotation_count;
    evaluate_pass(p);
    FxShader *s = p->vs && p->vs->signature ? p->vs : p->gs && p->gs->signature ? p->gs : NULL;
    if (s) {
        desc->pIAInputSignature = s->signature->lpVtbl->GetBufferPointer(s->signature);
        desc->IAInputSignatureSize = s->signature->lpVtbl->GetBufferSize(s->signature);
    }
    desc->StencilRef = p->stencil_ref;
    desc->SampleMask = p->sample_mask;
    memcpy(desc->BlendFactor, p->blend_factor, sizeof(desc->BlendFactor));
    return S_OK;
}

/* the variable of the pass's shader and its index there, an anonymous one for inline and null shaders. */
static HRESULT shader_desc(FxPass *p, FxShader *s, UINT null_index, D3D10_PASS_SHADER_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    evaluate_pass(p);
    desc->ShaderIndex = 0;
    desc->pShaderVariable = (ID3D10EffectShaderVariable *)&FxInvalidVariable;
    if (!s)
        return S_OK;
    if (s >= null_shaders && s < null_shaders + 3) {
        desc->pShaderVariable = (ID3D10EffectShaderVariable *)&FxAnonymousNull[null_index];
        return S_OK;
    }
    if (!s->variable) {
        FxVariable *v = FxAlloc(p->effect, sizeof(*v));
        FxType *t = v ? FxAlloc(p->effect, sizeof(*t)) : NULL;
        if (!t)
            return E_OUTOFMEMORY;
        t->kind = FX_OBJECT;
        t->object = s->kind;
        t->name = s->kind == FX_VERTEXSHADER  ? "vertexshader"
                  : s->kind == FX_PIXELSHADER ? "pixelshader"
                                              : "geometryshader";
        FxInitType(t);
        v->effect = p->effect;
        v->type = t;
        v->name = "$Anonymous";
        v->top = v;
        v->bind_point = ~0u;
        v->data = (BYTE *)s;
        FxInitVariable(v);
        s->variable = v;
    }
    desc->pShaderVariable = (ID3D10EffectShaderVariable *)s->variable;
    desc->ShaderIndex = s->variable_index;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pass_vs_desc(ID3D10EffectPass *iface, D3D10_PASS_SHADER_DESC *desc)
{
    return shader_desc(pass_impl(iface), pass_impl(iface)->vs, 1, desc);
}

static HRESULT STDMETHODCALLTYPE pass_gs_desc(ID3D10EffectPass *iface, D3D10_PASS_SHADER_DESC *desc)
{
    return shader_desc(pass_impl(iface), pass_impl(iface)->gs, 2, desc);
}

static HRESULT STDMETHODCALLTYPE pass_ps_desc(ID3D10EffectPass *iface, D3D10_PASS_SHADER_DESC *desc)
{
    return shader_desc(pass_impl(iface), pass_impl(iface)->ps, 0, desc);
}

static ID3D10EffectVariable *STDMETHODCALLTYPE pass_annotation_by_index(ID3D10EffectPass *iface, UINT index)
{
    return FxAnnotationByIndex(pass_impl(iface)->annotation_count, pass_impl(iface)->annotations, index);
}

static ID3D10EffectVariable *STDMETHODCALLTYPE pass_annotation_by_name(ID3D10EffectPass *iface, LPCSTR name)
{
    return FxAnnotationByName(pass_impl(iface)->annotation_count, pass_impl(iface)->annotations, name);
}

static HRESULT STDMETHODCALLTYPE pass_apply(ID3D10EffectPass *iface, UINT flags)
{
    apply_pass(pass_impl(iface));
    return S_OK;
}

static void mask_shader(D3D10_STATE_BLOCK_MASK *mask, FxShader *s, D3D10_DEVICE_STATE_TYPES stage)
{
    D3D10StateBlockMaskEnableCapture(mask, stage, 0, 1);
    for (UINT i = 0; i < s->binding_count; i++) {
        FxBinding *b = &s->bindings[i];
        D3D10_DEVICE_STATE_TYPES kind = b->kind == FX_BIND_CB        ? stage + 3
                                        : b->kind == FX_BIND_SAMPLER ? stage + 1
                                                                     : stage + 2;
        D3D10StateBlockMaskEnableCapture(mask, kind, b->slot, b->count);
    }
}

/* the states the pass sets. */
static HRESULT STDMETHODCALLTYPE pass_mask(ID3D10EffectPass *iface, D3D10_STATE_BLOCK_MASK *mask)
{
    FxPass *p = pass_impl(iface);
    if (!mask)
        return E_INVALIDARG;
    evaluate_pass(p);
    if (p->blend)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_OM_BLEND_STATE, 0, 1);
    if (p->depth_stencil)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_OM_DEPTH_STENCIL_STATE, 0, 1);
    if (p->rasterizer)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_RS_RASTERIZER_STATE, 0, 1);
    if (p->rtv_count)
        D3D10StateBlockMaskEnableCapture(mask, D3D10_DST_OM_RENDER_TARGETS, 0, 1);
    if (p->vs)
        mask_shader(mask, p->vs, D3D10_DST_VS);
    if (p->ps)
        mask_shader(mask, p->ps, D3D10_DST_PS);
    if (p->gs)
        mask_shader(mask, p->gs, D3D10_DST_GS);
    return S_OK;
}

static const ID3D10EffectPassVtbl pass_vtbl = {
    pass_is_valid,           pass_get_desc, pass_vs_desc, pass_gs_desc, pass_ps_desc, pass_annotation_by_index,
    pass_annotation_by_name, pass_apply,    pass_mask,
};

/* the invalid pass and technique, which other objects' getters hand out for what is not there. */
static BOOL STDMETHODCALLTYPE invalid_is_valid(void *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE invalid_desc(void *iface, void *desc) { return E_FAIL; }

static ID3D10EffectVariable *STDMETHODCALLTYPE invalid_annotation(void *iface, const void *which)
{
    return &FxInvalidVariable.iface;
}

static HRESULT STDMETHODCALLTYPE invalid_apply(void *iface, UINT flags) { return E_FAIL; }

static const ID3D10EffectPassVtbl invalid_pass_vtbl = {
    VTBL_FN(invalid_is_valid),   VTBL_FN(invalid_desc),  VTBL_FN(invalid_desc),
    VTBL_FN(invalid_desc),       VTBL_FN(invalid_desc),  VTBL_FN(invalid_annotation),
    VTBL_FN(invalid_annotation), VTBL_FN(invalid_apply), VTBL_FN(invalid_desc),
};

static ID3D10EffectPass invalid_pass = {&invalid_pass_vtbl};

static ID3D10EffectPass *STDMETHODCALLTYPE invalid_get_pass(void *iface, const void *which) { return &invalid_pass; }

static const ID3D10EffectTechniqueVtbl invalid_technique_vtbl = {
    VTBL_FN(invalid_is_valid), VTBL_FN(invalid_desc),     VTBL_FN(invalid_annotation), VTBL_FN(invalid_annotation),
    VTBL_FN(invalid_get_pass), VTBL_FN(invalid_get_pass), VTBL_FN(invalid_desc),
};

static ID3D10EffectTechnique invalid_technique = {&invalid_technique_vtbl};

/* ------------------------------------------------------------------ techniques */

static FxTechnique *technique_impl(ID3D10EffectTechnique *iface)
{
    return CONTAINING_RECORD(iface, FxTechnique, iface);
}

static BOOL STDMETHODCALLTYPE technique_is_valid(ID3D10EffectTechnique *iface) { return TRUE; }

static HRESULT STDMETHODCALLTYPE technique_get_desc(ID3D10EffectTechnique *iface, D3D10_TECHNIQUE_DESC *desc)
{
    FxTechnique *t = technique_impl(iface);
    if (!desc)
        return E_INVALIDARG;
    desc->Name = t->name;
    desc->Passes = t->pass_count;
    desc->Annotations = t->annotation_count;
    return S_OK;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE technique_annotation_by_index(ID3D10EffectTechnique *iface, UINT index)
{
    return FxAnnotationByIndex(technique_impl(iface)->annotation_count, technique_impl(iface)->annotations, index);
}

static ID3D10EffectVariable *STDMETHODCALLTYPE technique_annotation_by_name(ID3D10EffectTechnique *iface, LPCSTR name)
{
    return FxAnnotationByName(technique_impl(iface)->annotation_count, technique_impl(iface)->annotations, name);
}

static ID3D10EffectPass *STDMETHODCALLTYPE technique_pass_by_index(ID3D10EffectTechnique *iface, UINT index)
{
    FxTechnique *t = technique_impl(iface);
    return index < t->pass_count ? &t->passes[index].iface : &invalid_pass;
}

static ID3D10EffectPass *STDMETHODCALLTYPE technique_pass_by_name(ID3D10EffectTechnique *iface, LPCSTR name)
{
    FxTechnique *t = technique_impl(iface);
    for (UINT i = 0; name && i < t->pass_count; i++)
        if (t->passes[i].name && !strcmp(t->passes[i].name, name))
            return &t->passes[i].iface;
    return &invalid_pass;
}

static HRESULT STDMETHODCALLTYPE technique_mask(ID3D10EffectTechnique *iface, D3D10_STATE_BLOCK_MASK *mask)
{
    FxTechnique *t = technique_impl(iface);
    if (!mask)
        return E_INVALIDARG;
    for (UINT i = 0; i < t->pass_count; i++)
        t->passes[i].iface.lpVtbl->ComputeStateBlockMask(&t->passes[i].iface, mask);
    return S_OK;
}

static const ID3D10EffectTechniqueVtbl technique_vtbl = {
    technique_is_valid,
    technique_get_desc,
    technique_annotation_by_index,
    technique_annotation_by_name,
    technique_pass_by_index,
    technique_pass_by_name,
    technique_mask,
};

/* ------------------------------------------------------------------ the effect */

static FxEffect *effect_impl(ID3D10Effect *iface) { return CONTAINING_RECORD(iface, FxEffect, iface); }

static void destroy(FxEffect *e)
{
    for (UINT i = 0; i < e->buffer_count; i++) {
        FxBuffer *b = &e->buffers[i];
        if (b->buffer)
            b->buffer->lpVtbl->Release(b->buffer);
        if (b->view)
            b->view->lpVtbl->Release(b->view);
    }
    for (UINT i = 0; i < e->block_count; i++)
        if (e->blocks[i].state)
            e->blocks[i].state->lpVtbl->Release(e->blocks[i].state);
    for (UINT i = 0; i < e->shader_count; i++) {
        if (e->shaders[i].object)
            e->shaders[i].object->lpVtbl->Release(e->shaders[i].object);
        if (e->shaders[i].signature)
            e->shaders[i].signature->lpVtbl->Release(e->shaders[i].signature);
    }
    for (UINT i = 0; i < e->variable_count; i++) {
        FxVariable *v = &e->variables[i];
        UINT o = v->type ? v->type->object : 0;
        if (v->type && v->type->kind == FX_OBJECT && v->data &&
            ((o >= FX_TEXTURE && o <= FX_TEXTURECUBE) || o == FX_BUFFER || o == FX_RENDERTARGETVIEW ||
             o == FX_DEPTHSTENCILVIEW))
            for (UINT k = 0; k < FxElementCount(v->type); k++) {
                IUnknown *view = ((IUnknown **)v->data)[k];
                if (view)
                    view->lpVtbl->Release(view);
            }
    }
    if (e->device)
        e->device->lpVtbl->Release(e->device);
    if (e->pool)
        e->pool->iface.lpVtbl->Release(&e->pool->iface);
    for (FxArena *a = e->arena, *next; a; a = next) {
        next = a->next;
        HeapFree(GetProcessHeap(), 0, a);
    }
    HeapFree(GetProcessHeap(), 0, e);
}

/* an effect answers the iid of ID3D10EffectPool only, as d3d10.dll's does, a pool it too. */
static HRESULT STDMETHODCALLTYPE effect_query(ID3D10Effect *iface, REFIID riid, void **out)
{
    FxEffect *e = effect_impl(iface);
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    if (!IsEqualGUID(riid, &IID_ID3D10EffectPool))
        return E_NOINTERFACE;
    *out = e->is_pool ? (void *)&e->pool_iface : (void *)&e->iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE effect_add_ref(ID3D10Effect *iface)
{
    return InterlockedIncrement(&effect_impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE effect_release(ID3D10Effect *iface)
{
    FxEffect *e = effect_impl(iface);
    ULONG refs = InterlockedDecrement(&e->refs);
    if (!refs)
        destroy(e);
    return refs;
}

static BOOL STDMETHODCALLTYPE effect_is_valid(ID3D10Effect *iface) { return TRUE; }

static BOOL STDMETHODCALLTYPE effect_is_pool(ID3D10Effect *iface) { return effect_impl(iface)->is_pool; }

static HRESULT STDMETHODCALLTYPE effect_get_device(ID3D10Effect *iface, ID3D10Device **device)
{
    FxEffect *e = effect_impl(iface);
    if (!device)
        return E_INVALIDARG;
    e->device->lpVtbl->AddRef(e->device);
    *device = e->device;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE effect_get_desc(ID3D10Effect *iface, D3D10_EFFECT_DESC *desc)
{
    FxEffect *e = effect_impl(iface);
    if (!desc)
        return E_INVALIDARG;
    desc->IsChildEffect = e->pool != NULL;
    desc->SharedConstantBuffers = e->pool ? e->pool->buffer_count : 0;
    desc->SharedGlobalVariables = e->pool ? e->pool->variable_count : 0;
    desc->ConstantBuffers = e->buffer_count;
    desc->GlobalVariables = e->variable_count;
    desc->Techniques = e->technique_count;
    return S_OK;
}

/* indices past the effect's own are its pool's, as are names it does not have. */
static ID3D10EffectConstantBuffer *STDMETHODCALLTYPE effect_buffer_by_index(ID3D10Effect *iface, UINT index)
{
    FxEffect *e = effect_impl(iface);
    if (index < e->buffer_count)
        return (ID3D10EffectConstantBuffer *)&e->buffers[index].var.iface;
    if (e->pool)
        return e->pool->iface.lpVtbl->GetConstantBufferByIndex(&e->pool->iface, index - e->buffer_count);
    return (ID3D10EffectConstantBuffer *)&FxInvalidBuffer.iface;
}

static ID3D10EffectConstantBuffer *STDMETHODCALLTYPE effect_buffer_by_name(ID3D10Effect *iface, LPCSTR name)
{
    FxBuffer *b = name && !effect_impl(iface)->optimized ? find_buffer(effect_impl(iface), name) : NULL;
    return (ID3D10EffectConstantBuffer *)(b ? &b->var.iface : &FxInvalidBuffer.iface);
}

static ID3D10EffectVariable *STDMETHODCALLTYPE effect_variable_by_index(ID3D10Effect *iface, UINT index)
{
    FxEffect *e = effect_impl(iface);
    if (index < e->variable_count)
        return &e->variables[index].iface;
    if (e->pool)
        return e->pool->iface.lpVtbl->GetVariableByIndex(&e->pool->iface, index - e->variable_count);
    return &FxInvalidVariable.iface;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE effect_variable_by_name(ID3D10Effect *iface, LPCSTR name)
{
    FxVariable *v = name && !effect_impl(iface)->optimized ? FxFindVariable(effect_impl(iface), name) : NULL;
    return v ? &v->iface : &FxInvalidVariable.iface;
}

/* semantics compare without regard to case. */
static ID3D10EffectVariable *STDMETHODCALLTYPE effect_variable_by_semantic(ID3D10Effect *iface, LPCSTR semantic)
{
    for (FxEffect *e = effect_impl(iface); semantic && !effect_impl(iface)->optimized && e; e = e->pool)
        for (UINT i = 0; i < e->variable_count; i++)
            if (e->variables[i].semantic && !lstrcmpiA(e->variables[i].semantic, semantic))
                return &e->variables[i].iface;
    return &FxInvalidVariable.iface;
}

static ID3D10EffectTechnique *STDMETHODCALLTYPE effect_technique_by_index(ID3D10Effect *iface, UINT index)
{
    FxEffect *e = effect_impl(iface);
    return index < e->technique_count ? &e->techniques[index].iface : &invalid_technique;
}

static ID3D10EffectTechnique *STDMETHODCALLTYPE effect_technique_by_name(ID3D10Effect *iface, LPCSTR name)
{
    FxEffect *e = effect_impl(iface);
    for (UINT i = 0; name && !e->optimized && i < e->technique_count; i++)
        if (e->techniques[i].name && !strcmp(e->techniques[i].name, name))
            return &e->techniques[i].iface;
    return &invalid_technique;
}

/* optimizing lets go of the names, which the effect then cannot be searched by. */
static HRESULT STDMETHODCALLTYPE effect_optimize(ID3D10Effect *iface)
{
    effect_impl(iface)->optimized = TRUE;
    return S_OK;
}

static BOOL STDMETHODCALLTYPE effect_is_optimized(ID3D10Effect *iface) { return effect_impl(iface)->optimized; }

static const ID3D10EffectVtbl effect_vtbl = {
    effect_query,
    effect_add_ref,
    effect_release,
    effect_is_valid,
    effect_is_pool,
    effect_get_device,
    effect_get_desc,
    effect_buffer_by_index,
    effect_buffer_by_name,
    effect_variable_by_index,
    effect_variable_by_name,
    effect_variable_by_semantic,
    effect_technique_by_index,
    effect_technique_by_name,
    effect_optimize,
    effect_is_optimized,
};

static FxEffect *pool_impl(ID3D10EffectPool *iface) { return CONTAINING_RECORD(iface, FxEffect, pool_iface); }

static HRESULT STDMETHODCALLTYPE pool_query(ID3D10EffectPool *iface, REFIID riid, void **out)
{
    return effect_query(&pool_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE pool_add_ref(ID3D10EffectPool *iface)
{
    return effect_add_ref(&pool_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE pool_release(ID3D10EffectPool *iface)
{
    return effect_release(&pool_impl(iface)->iface);
}

static ID3D10Effect *STDMETHODCALLTYPE pool_as_effect(ID3D10EffectPool *iface) { return &pool_impl(iface)->iface; }

static const ID3D10EffectPoolVtbl pool_vtbl = {pool_query, pool_add_ref, pool_release, pool_as_effect};

/* ------------------------------------------------------------------ loading */

/* the tag, fx_4_0's or fx_4_1's, is not read, as d3d10.dll does not. */
static HRESULT load(FxEffect *e, const void *data, SIZE_T length)
{
    uint32_t size;
    if (!D3d10ContainerValid(data, length))
        return E_FAIL;
    const BYTE *chunk = dxbc_find_chunk(data, length, DXBC_FOURCC('F', 'X', '1', '0'), &size);
    if (!chunk || size < 76)
        return E_FAIL;
    FxReader r = {e};
    BYTE *copy = FxAlloc(e, size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, chunk, size);
    r.base = copy;
    r.size = size;
    for (UINT i = 0; i < 19; i++)
        r.header[i] = read32(copy + 4 * i);
    if (r.header[8] > size - 76)
        return E_FAIL;
    if ((r.header[4] || r.header[5] || r.header[6]) && !e->pool)
        return E_FAIL;
    if (r.header[18] > r.header[17])
        return E_FAIL;
    r.data = copy + 76;
    r.data_size = r.header[8];
    r.pos = 76 + r.header[8];
    UINT variables = r.header[2] + r.header[3];
    UINT blocks = r.header[11] + r.header[12] + r.header[13] + r.header[14];
    if ((variables && !(e->variables = FxAlloc(e, (SIZE_T)variables * sizeof(*e->variables)))) ||
        (blocks && !(e->blocks = FxAlloc(e, (SIZE_T)blocks * sizeof(*e->blocks)))) ||
        (r.header[17] && !(e->shaders = FxAlloc(e, (SIZE_T)r.header[17] * sizeof(*e->shaders)))))
        return E_OUTOFMEMORY;
    e->block_capacity = blocks;
    e->shader_capacity = r.header[17];
    HRESULT hr = read_buffers(&r);
    if (SUCCEEDED(hr))
        hr = read_objects(&r);
    if (SUCCEEDED(hr))
        hr = read_shared(&r);
    if (SUCCEEDED(hr))
        hr = read_techniques(&r);
    if (SUCCEEDED(hr) && e->variable_count != variables)
        hr = E_FAIL;
    HeapFree(GetProcessHeap(), 0, r.types);
    return hr;
}

static HRESULT create(void *data, SIZE_T length, ID3D10Device *device, FxEffect *pool, BOOL is_pool, FxEffect **out)
{
    *out = NULL;
    if (!device)
        return D3DERR_INVALIDCALL;
    FxEffect *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    e->iface.lpVtbl = &effect_vtbl;
    e->pool_iface.lpVtbl = &pool_vtbl;
    e->refs = 1;
    e->is_pool = is_pool;
    if ((e->pool = pool))
        pool->iface.lpVtbl->AddRef(&pool->iface);
    e->device = device;
    device->lpVtbl->AddRef(device);
    HRESULT hr = load(e, data, length);
    if (SUCCEEDED(hr))
        hr = bind_to_device(e);
    if (FAILED(hr)) {
        destroy(e);
        return hr;
    }
    *out = e;
    return S_OK;
}

/* a child effect takes a pool, and only it does. */
D3D10API HRESULT WINAPI D3D10CreateEffectFromMemory(void *data, SIZE_T length, UINT flags, ID3D10Device *device,
                                                    ID3D10EffectPool *pool, ID3D10Effect **effect)
{
    FxEffect *e;
    if (!effect)
        return E_INVALIDARG;
    *effect = NULL;
    if ((flags & D3D10_EFFECT_COMPILE_CHILD_EFFECT) ? !pool : pool != NULL)
        return E_INVALIDARG;
    HRESULT hr = create(data, length, device, pool ? pool_impl(pool) : NULL, FALSE, &e);
    if (SUCCEEDED(hr))
        *effect = &e->iface;
    return hr;
}

D3D10API HRESULT WINAPI D3D10CreateEffectPoolFromMemory(void *data, SIZE_T length, UINT flags, ID3D10Device *device,
                                                        ID3D10EffectPool **pool)
{
    FxEffect *e;
    if (!pool)
        return E_INVALIDARG;
    *pool = NULL;
    HRESULT hr = create(data, length, device, NULL, TRUE, &e);
    if (SUCCEEDED(hr))
        *pool = &e->pool_iface;
    return hr;
}
