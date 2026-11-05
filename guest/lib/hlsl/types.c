#include "internal.h"

#define CACHE_SIZE (BT_COUNT * 3 * 4 * 4 * 4)

static HlType *simple(Hlsl *h, HlType **slot, uint8_t kind)
{
    if (!*slot) {
        *slot = hl_alloc(h, sizeof(HlType));
        (*slot)->kind = kind;
    }
    return *slot;
}

HlType *hl_type_error(Hlsl *h) { return simple(h, &h->error_type, TY_ERROR); }
HlType *hl_type_void(Hlsl *h) { return simple(h, &h->void_type, TY_VOID); }
HlType *hl_type_string(Hlsl *h) { return simple(h, &h->string_type, TY_STRING); }

HlType *hl_type_numeric(Hlsl *h, uint8_t base, uint8_t rows, uint8_t cols, uint8_t kind)
{
    if (!h->type_cache)
        h->type_cache = hl_alloc(h, CACHE_SIZE * sizeof(HlType *));
    if (rows < 1 || rows > 4 || cols < 1 || cols > 4 || base >= BT_COUNT)
        return hl_type_error(h);
    uint32_t k = kind == TY_SCALAR ? 0 : kind == TY_VECTOR ? 1 : 2;
    uint32_t at = (((base * 3 + k) * 4 + (rows - 1u)) * 4 + (cols - 1u)) * 4;
    HlType **slot = &h->type_cache[at];
    if (!*slot) {
        HlType *t = hl_alloc(h, sizeof(HlType));
        t->kind = kind;
        t->base = base;
        t->rows = kind == TY_MATRIX ? rows : 1;
        t->cols = kind == TY_SCALAR ? 1 : cols;
        *slot = t;
    }
    return *slot;
}

HlType *hl_type_majored(Hlsl *h, HlType *t, uint8_t modifiers)
{
    if (t->kind != TY_MATRIX)
        return t;
    modifiers &= TM_ROW_MAJOR | TM_COLUMN_MAJOR;
    if ((t->modifiers & (TM_ROW_MAJOR | TM_COLUMN_MAJOR)) == modifiers)
        return t;
    uint32_t at = (((t->base * 3u + 2) * 4 + (t->rows - 1u)) * 4 + (t->cols - 1u)) * 4 + modifiers;
    HlType **slot = &h->type_cache[at];
    if (!*slot) {
        HlType *n = hl_alloc(h, sizeof(HlType));
        *n = *t;
        n->name = nullptr;
        n->modifiers = (uint8_t)((t->modifiers & ~(TM_ROW_MAJOR | TM_COLUMN_MAJOR)) | modifiers);
        *slot = n;
    }
    return *slot;
}

HlType *hl_type_array(Hlsl *h, HlType *elem, uint32_t count)
{
    HlType *t = hl_alloc(h, sizeof(HlType));
    t->kind = TY_ARRAY;
    t->elem = elem;
    t->count = count;
    return t;
}

HlType *hl_type_object(Hlsl *h, uint8_t object, HlType *elem, uint32_t samples)
{
    HlType *t = hl_alloc(h, sizeof(HlType));
    t->kind = TY_OBJECT;
    t->object = object;
    t->elem = elem;
    t->samples = samples;
    return t;
}

HlType *hl_type_struct(Hlsl *h, HlStruct *st)
{
    HlType *t = hl_alloc(h, sizeof(HlType));
    t->kind = TY_STRUCT;
    t->st = st;
    return t;
}

HlType *hl_type_with_base(Hlsl *h, const HlType *t, uint8_t base)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
    case TY_MATRIX: {
        HlType *n = hl_type_numeric(h, base, t->rows, t->cols, t->kind);
        return t->kind == TY_MATRIX ? hl_type_majored(h, n, t->modifiers) : n;
    }
    case TY_ARRAY:
        return hl_type_array(h, hl_type_with_base(h, t->elem, base), t->count);
    default:
        return (HlType *)t;
    }
}

bool hl_type_equal(const HlType *a, const HlType *b)
{
    if (a == b)
        return true;
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
    case TY_MATRIX:
        return a->base == b->base && a->rows == b->rows && a->cols == b->cols;
    case TY_ARRAY:
        return a->count == b->count && hl_type_equal(a->elem, b->elem);
    case TY_STRUCT:
        return a->st == b->st;
    case TY_OBJECT:
        return a->object == b->object && a->samples == b->samples &&
               (!a->elem || !b->elem || hl_type_equal(a->elem, b->elem));
    default:
        return true;
    }
}

uint32_t hl_type_components(const HlType *t)
{
    switch (t->kind) {
    case TY_SCALAR:
        return 1;
    case TY_VECTOR:
        return t->cols;
    case TY_MATRIX:
        return (uint32_t)t->rows * t->cols;
    case TY_ARRAY: {
        uint64_t n = (uint64_t)t->count * hl_type_components(t->elem);
        return n > 0x1000000 ? 0x1000000 : (uint32_t)n;
    }
    case TY_STRUCT: {
        uint32_t n = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++)
            n += hl_type_components(t->st->fields[i].type);
        return n;
    }
    default:
        return 0;
    }
}

bool hl_type_has_object(const HlType *t)
{
    switch (t->kind) {
    case TY_OBJECT:
        return true;
    case TY_ARRAY:
        return hl_type_has_object(t->elem);
    case TY_STRUCT:
        for (uint32_t i = 0; i < t->st->field_count; i++)
            if (hl_type_has_object(t->st->fields[i].type))
                return true;
        return false;
    default:
        return false;
    }
}

uint8_t hl_type_component_base(const HlType *t, uint32_t index)
{
    switch (t->kind) {
    case TY_SCALAR:
    case TY_VECTOR:
    case TY_MATRIX:
        return t->base;
    case TY_ARRAY: {
        uint32_t n = hl_type_components(t->elem);
        return n ? hl_type_component_base(t->elem, index % n) : BT_FLOAT;
    }
    case TY_STRUCT:
        for (uint32_t i = 0; i < t->st->field_count; i++) {
            uint32_t n = hl_type_components(t->st->fields[i].type);
            if (index < n)
                return hl_type_component_base(t->st->fields[i].type, index);
            index -= n;
        }
        return BT_FLOAT;
    default:
        return BT_FLOAT;
    }
}

static const char *const base_names[BT_COUNT] = {
    [BT_BOOL] = "bool",         [BT_LIT_INT] = "int",         [BT_MIN12INT] = "min12int",
    [BT_MIN16INT] = "min16int", [BT_INT] = "int",             [BT_MIN16UINT] = "min16uint",
    [BT_UINT] = "uint",         [BT_LIT_FLOAT] = "float",     [BT_MIN10FLOAT] = "min10float",
    [BT_MIN16FLOAT] = "min16float", [BT_HALF] = "half",       [BT_FLOAT] = "float",
    [BT_DOUBLE] = "double",
};

const char *hl_base_name(uint8_t base) { return base < BT_COUNT ? base_names[base] : "?"; }

bool hl_base_float(uint8_t base) { return base >= BT_LIT_FLOAT; }
bool hl_base_int(uint8_t base) { return base >= BT_LIT_INT && base <= BT_UINT; }
bool hl_base_unsigned(uint8_t base) { return base == BT_UINT || base == BT_MIN16UINT; }

uint8_t hl_base_storage(uint8_t base)
{
    switch (base) {
    case BT_BOOL:
        return BT_BOOL;
    case BT_UINT:
    case BT_MIN16UINT:
        return BT_UINT;
    case BT_DOUBLE:
        return BT_DOUBLE;
    default:
        return hl_base_float(base) ? BT_FLOAT : BT_INT;
    }
}

static const char *const object_names[OBJ_COUNT] = {
    [OBJ_SAMPLER] = "SamplerState",
    [OBJ_SAMPLER_COMPARISON] = "SamplerComparisonState",
    [OBJ_SAMPLER1D] = "sampler1D",
    [OBJ_SAMPLER2D] = "sampler2D",
    [OBJ_SAMPLER3D] = "sampler3D",
    [OBJ_SAMPLERCUBE] = "samplerCUBE",
    [OBJ_TEXTURE] = "texture",
    [OBJ_BUFFER] = "Buffer",
    [OBJ_TEXTURE1D] = "Texture1D",
    [OBJ_TEXTURE1DARRAY] = "Texture1DArray",
    [OBJ_TEXTURE2D] = "Texture2D",
    [OBJ_TEXTURE2DARRAY] = "Texture2DArray",
    [OBJ_TEXTURE2DMS] = "Texture2DMS",
    [OBJ_TEXTURE2DMSARRAY] = "Texture2DMSArray",
    [OBJ_TEXTURE3D] = "Texture3D",
    [OBJ_TEXTURECUBE] = "TextureCube",
    [OBJ_TEXTURECUBEARRAY] = "TextureCubeArray",
    [OBJ_STRUCTURED_BUFFER] = "StructuredBuffer",
    [OBJ_BYTE_ADDRESS_BUFFER] = "ByteAddressBuffer",
    [OBJ_RWBUFFER] = "RWBuffer",
    [OBJ_RWTEXTURE1D] = "RWTexture1D",
    [OBJ_RWTEXTURE1DARRAY] = "RWTexture1DArray",
    [OBJ_RWTEXTURE2D] = "RWTexture2D",
    [OBJ_RWTEXTURE2DARRAY] = "RWTexture2DArray",
    [OBJ_RWTEXTURE3D] = "RWTexture3D",
    [OBJ_RWSTRUCTURED_BUFFER] = "RWStructuredBuffer",
    [OBJ_RWBYTE_ADDRESS_BUFFER] = "RWByteAddressBuffer",
    [OBJ_APPEND_STRUCTURED_BUFFER] = "AppendStructuredBuffer",
    [OBJ_CONSUME_STRUCTURED_BUFFER] = "ConsumeStructuredBuffer",
    [OBJ_INPUT_PATCH] = "InputPatch",
    [OBJ_OUTPUT_PATCH] = "OutputPatch",
    [OBJ_POINT_STREAM] = "PointStream",
    [OBJ_LINE_STREAM] = "LineStream",
    [OBJ_TRIANGLE_STREAM] = "TriangleStream",
    [OBJ_CONSTANT_BUFFER] = "ConstantBuffer",
    [OBJ_TEXTURE_BUFFER] = "TextureBuffer",
};

const char *hl_object_name(uint8_t object) { return object < OBJ_COUNT && object_names[object] ? object_names[object] : "object"; }

bool hl_object_is_texture(uint8_t object) { return object >= OBJ_TEXTURE && object <= OBJ_BYTE_ADDRESS_BUFFER; }
bool hl_object_is_uav(uint8_t object)
{
    return object >= OBJ_RWBUFFER && object <= OBJ_CONSUME_STRUCTURED_BUFFER;
}
bool hl_object_is_sampler(uint8_t object) { return object >= OBJ_SAMPLER && object <= OBJ_SAMPLERCUBE; }

uint32_t hl_object_dims(uint8_t object)
{
    switch (object) {
    case OBJ_BUFFER:
    case OBJ_RWBUFFER:
    case OBJ_TEXTURE1D:
    case OBJ_TEXTURE1DARRAY:
    case OBJ_RWTEXTURE1D:
    case OBJ_RWTEXTURE1DARRAY:
    case OBJ_SAMPLER1D:
    case OBJ_STRUCTURED_BUFFER:
    case OBJ_RWSTRUCTURED_BUFFER:
    case OBJ_BYTE_ADDRESS_BUFFER:
    case OBJ_RWBYTE_ADDRESS_BUFFER:
        return 1;
    case OBJ_TEXTURE3D:
    case OBJ_RWTEXTURE3D:
    case OBJ_TEXTURECUBE:
    case OBJ_TEXTURECUBEARRAY:
    case OBJ_SAMPLER3D:
    case OBJ_SAMPLERCUBE:
        return 3;
    default:
        return 2;
    }
}

bool hl_object_is_array(uint8_t object)
{
    return object == OBJ_TEXTURE1DARRAY || object == OBJ_TEXTURE2DARRAY || object == OBJ_TEXTURE2DMSARRAY ||
           object == OBJ_TEXTURECUBEARRAY || object == OBJ_RWTEXTURE1DARRAY || object == OBJ_RWTEXTURE2DARRAY;
}

const char *hl_type_name(Hlsl *h, const HlType *t)
{
    if (t->name)
        return t->name;
    switch (t->kind) {
    case TY_ERROR:
        return "<error>";
    case TY_VOID:
        return "void";
    case TY_STRING:
        return "string";
    case TY_SCALAR:
        return hl_base_name(t->base);
    case TY_VECTOR:
        return hl_sprintf(h, "%s%u", hl_base_name(t->base), t->cols);
    case TY_MATRIX:
        return hl_sprintf(h, "%s%ux%u", hl_base_name(t->base), t->rows, t->cols);
    case TY_ARRAY:
        return t->count ? hl_sprintf(h, "%s[%u]", hl_type_name(h, t->elem), t->count)
                        : hl_sprintf(h, "%s[]", hl_type_name(h, t->elem));
    case TY_STRUCT:
        return t->st->name ? t->st->name : "<unnamed>";
    case TY_OBJECT:
        if (t->elem && t->object != OBJ_BYTE_ADDRESS_BUFFER && t->object != OBJ_RWBYTE_ADDRESS_BUFFER)
            return hl_sprintf(h, "%s<%s>", hl_object_name(t->object), hl_type_name(h, t->elem));
        return hl_object_name(t->object);
    }
    return "?";
}

bool hl_type_row_major(const HlType *t, bool row_major_default)
{
    if (t->modifiers & TM_ROW_MAJOR)
        return true;
    if (t->modifiers & TM_COLUMN_MAJOR)
        return false;
    return row_major_default;
}

/* bytes from the start of the first register; aggregates start on a register. */
uint32_t hl_type_cb_size(const HlType *t, bool row_major_default)
{
    switch (t->kind) {
    case TY_SCALAR:
        return t->base == BT_DOUBLE ? 8 : 4;
    case TY_VECTOR:
        return t->cols * (t->base == BT_DOUBLE ? 8u : 4u);
    case TY_MATRIX: {
        uint32_t size = t->base == BT_DOUBLE ? 8 : 4;
        if (hl_type_row_major(t, row_major_default))
            return (t->rows - 1u) * 16 + t->cols * size;
        return (t->cols - 1u) * 16 + t->rows * size;
    }
    case TY_ARRAY: {
        if (!t->count)
            return 0;
        uint32_t elem = hl_type_cb_size(t->elem, row_major_default);
        uint32_t stride = (elem + 15) & ~15u;
        uint64_t size = (uint64_t)stride * (t->count - 1) + elem;
        return size > 0x10000000 ? 0x10000000 : (uint32_t)size;
    }
    case TY_STRUCT: {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < t->st->field_count; i++) {
            const HlType *f = t->st->fields[i].type;
            if (f->kind == TY_OBJECT)
                continue;
            uint32_t size = hl_type_cb_size(f, row_major_default);
            if (f->kind == TY_ARRAY || f->kind == TY_STRUCT || f->kind == TY_MATRIX ||
                ((offset & 15) + size > 16))
                offset = (offset + 15) & ~15u;
            offset += size;
        }
        return offset;
    }
    default:
        return 0;
    }
}
