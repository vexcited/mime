/*
 * compiles dxil to spir-v.
 *
 * each defined function of the entry point becomes a spir-v function made
 * of the blocks cfg.c structures, in their order. dxil ssa values map one
 * to one to spir-v ids and phis to OpPhi, whose operands are filled in once
 * every block is written. a value whose definition no longer dominates its
 * uses because of the added blocks goes through a function variable.
 * io.c, resource.c and ops.c lower the dx.op calls.
 *
 * resources go through direct3d 12's root signature, as with dxbc: see
 * dxbc.h.
 */
#include "internal.h"

int dxil_fail(Compiler *c, const char *error)
{
    if (!c->error)
        c->error = error;
    return -1;
}

void *dxil_alloc(Compiler *c, size_t size)
{
    void *p = bc_arena_alloc(&c->arena, size ? size : 1);
    if (!p)
        dxil_fail(c, "out of memory");
    return p;
}

uint32_t dxil_uconst(Compiler *c, uint32_t v) { return spv_const(&c->b, c->t_u32, v); }

uint32_t dxil_fconst(Compiler *c, float v)
{
    union {
        float f;
        uint32_t u;
    } bits = {v};
    return spv_const(&c->b, c->t_f32, bits.u);
}

void dxil_decorate(Compiler *c, uint32_t id, uint32_t decoration, uint32_t value)
{
    spv_decorate(&c->b, id, decoration, &value, 1);
}

static void add_interface(Compiler *c, uint32_t var)
{
    if (c->interface_count == c->interface_capacity) {
        uint32_t want = c->interface_capacity ? c->interface_capacity * 2 : 32;
        uint32_t *list = dxil_alloc(c, want * sizeof(uint32_t));
        if (!list)
            return;
        if (c->interface)
            __builtin_memcpy(list, c->interface, c->interface_count * sizeof(uint32_t));
        c->interface = list;
        c->interface_capacity = want;
    }
    c->interface[c->interface_count++] = var;
}

uint32_t dxil_global(Compiler *c, uint32_t storage, uint32_t type, uint32_t initializer)
{
    uint32_t var = spv_variable(&c->b, spv_type_pointer(&c->b, storage, type), storage, initializer);
    add_interface(c, var);
    return var;
}

uint32_t dxil_builtin(Compiler *c, uint32_t which, uint32_t storage, uint32_t type)
{
    for (uint32_t i = 0; i < c->builtin_count; i++)
        if (c->builtins[i].which == which && c->builtins[i].storage == storage)
            return c->builtins[i].var;
    uint32_t var = dxil_global(c, storage, type, 0);
    dxil_decorate(c, var, SpvDecorationBuiltIn, which);
    if (storage == SpvStorageClassInput && c->kind == KIND_PIXEL && type != c->t_bool &&
        (which == SpvBuiltInPrimitiveId || which == SpvBuiltInSampleId || which == SpvBuiltInLayer ||
         which == SpvBuiltInViewportIndex || which == SpvBuiltInViewIndex ||
         which == SpvBuiltInSubgroupLocalInvocationId || which == SpvBuiltInSubgroupSize ||
         which == SpvBuiltInSubgroupLtMask || which == SpvBuiltInShadingRateKHR))
        spv_decorate(&c->b, var, SpvDecorationFlat, nullptr, 0);
    if (c->builtin_count < 64) {
        c->builtins[c->builtin_count].which = which;
        c->builtins[c->builtin_count].storage = storage;
        c->builtins[c->builtin_count].var = var;
        c->builtin_count++;
    }
    return var;
}

/* ------------------------------------------------------------------ types */

static void make_types(Compiler *c)
{
    SpvBuilder *b = &c->b;
    c->t_void = spv_type_void(b);
    c->t_bool = spv_type_bool(b);
    c->t_f32 = spv_type_float(b, 32);
    c->t_u32 = spv_type_int(b, 32, 0);
    for (uint32_t n = 2; n <= 4; n++) {
        c->t_vec[n] = spv_type_vector(b, c->t_f32, n);
        c->t_uvec[n] = spv_type_vector(b, c->t_u32, n);
        c->t_bvec[n] = spv_type_vector(b, c->t_bool, n);
    }
    c->t_vec[1] = c->t_f32;
    c->t_uvec[1] = c->t_u32;
    c->t_bvec[1] = c->t_bool;
}

static uint32_t type_f16(Compiler *c)
{
    if (!c->t_f16) {
        spv_capability(&c->b, SpvCapabilityFloat16);
        c->t_f16 = spv_type_float(&c->b, 16);
    }
    return c->t_f16;
}

static uint32_t type_u16(Compiler *c)
{
    if (!c->t_u16) {
        spv_capability(&c->b, SpvCapabilityInt16);
        c->t_u16 = spv_type_int(&c->b, 16, 0);
    }
    return c->t_u16;
}

/* without native low precision, half and i16 are direct3d's minimum precision types and run in 32 bits. */
uint32_t dxil_type(Compiler *c, uint32_t t)
{
    if (t >= c->m.type_count) {
        dxil_fail(c, "a type out of range");
        return c->t_u32;
    }
    if (c->type_map[t])
        return c->type_map[t];
    const DxType *ty = &c->m.types[t];
    uint32_t r;
    switch (ty->kind) {
    case DT_VOID: r = c->t_void; break;
    case DT_INT:
        if (ty->width == 1) {
            r = c->t_bool;
        } else if (ty->width == 64) {
            if (!c->t_u64) {
                spv_capability(&c->b, SpvCapabilityInt64);
                c->t_u64 = spv_type_int(&c->b, 64, 0);
            }
            r = c->t_u64;
        } else if (ty->width == 16 && c->native16) {
            r = type_u16(c);
        } else {
            r = c->t_u32;
        }
        break;
    case DT_HALF: r = c->native16 ? type_f16(c) : c->t_f32; break;
    case DT_FLOAT: r = c->t_f32; break;
    case DT_DOUBLE:
        if (!c->t_f64) {
            spv_capability(&c->b, SpvCapabilityFloat64);
            c->t_f64 = spv_type_float(&c->b, 64);
        }
        r = c->t_f64;
        break;
    case DT_VECTOR: r = spv_type_vector(&c->b, dxil_type(c, ty->element), ty->count); break;
    case DT_ARRAY: r = spv_type_array(&c->b, dxil_type(c, ty->element), ty->count ? ty->count : 1); break;
    case DT_STRUCT: {
        uint32_t *members = dxil_alloc(c, (ty->count + 1) * sizeof(uint32_t));
        if (!members || !ty->count) {
            dxil_fail(c, "an empty structure");
            return c->t_u32;
        }
        for (uint32_t i = 0; i < ty->count; i++)
            members[i] = dxil_type(c, ty->members[i]);
        r = spv_type_struct(&c->b, members, ty->count);
        break;
    }
    default: dxil_fail(c, "a type spir-v has no value of"); return c->t_u32;
    }
    c->type_map[t] = r;
    return r;
}

uint32_t dxil_component_type(Compiler *c, uint32_t type)
{
    const DxType *t = &c->m.types[type];
    return t->kind == DT_VECTOR ? t->element : type;
}

static int is_float_type(Compiler *c, uint32_t type)
{
    uint8_t k = c->m.types[dxil_component_type(c, type)].kind;
    return k == DT_HALF || k == DT_FLOAT || k == DT_DOUBLE;
}

static int is_bool_type(Compiler *c, uint32_t type)
{
    const DxType *t = &c->m.types[dxil_component_type(c, type)];
    return t->kind == DT_INT && t->width == 1;
}

static int same_string(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int is_handle_type(Compiler *c, uint32_t type)
{
    const DxType *t = &c->m.types[type];
    return t->kind == DT_STRUCT && t->name && same_string(t->name, "dx.types.Handle");
}

/* ------------------------------------------------------------------ values */

Value *dxil_slot(Compiler *c, uint32_t id)
{
    if (id < c->m.value_count)
        return &c->module_values[id];
    id -= c->m.value_count;
    if (c->f && id < c->f->value_count)
        return &c->values[id];
    return nullptr;
}

static uint32_t half_to_float(uint32_t h)
{
    uint32_t sign = (h & 0x8000) << 16, exp = (h >> 10) & 0x1F, man = h & 0x3FF;
    if (exp == 0x1F)
        return sign | 0x7F800000 | man << 13;
    if (exp)
        return sign | (exp + 112) << 23 | man << 13;
    if (!man)
        return sign;
    /* a denormal half is a normal float. */
    exp = 113;
    while (!(man & 0x400)) {
        man <<= 1;
        exp--;
    }
    return sign | exp << 23 | (man & 0x3FF) << 13;
}

/* from the bits llvm stores. */
static uint32_t scalar_constant(Compiler *c, uint32_t type, uint64_t bits)
{
    const DxType *t = &c->m.types[type];
    uint32_t spv = dxil_type(c, type);
    switch (t->kind) {
    case DT_INT:
        if (t->width == 1)
            return spv_const_bool(&c->b, (int)(bits & 1));
        if (t->width == 64)
            return spv_const64(&c->b, spv, bits);
        if (t->width == 16 && c->native16)
            return spv_const(&c->b, spv, (uint32_t)bits & 0xFFFF);
        if (t->width == 8)
            return spv_const(&c->b, spv, (uint32_t)bits & 0xFF);
        return spv_const(&c->b, spv, (uint32_t)bits);
    case DT_HALF:
        return spv_const(&c->b, spv, c->native16 ? (uint32_t)bits & 0xFFFF : half_to_float((uint32_t)bits));
    case DT_FLOAT: return spv_const(&c->b, spv, (uint32_t)bits);
    case DT_DOUBLE: return spv_const64(&c->b, spv, bits);
    default: dxil_fail(c, "a constant of an unsupported type"); return dxil_uconst(c, 0);
    }
}

static uint32_t element_type(Compiler *c, uint32_t type, uint32_t index)
{
    const DxType *t = &c->m.types[type];
    if (t->kind == DT_STRUCT)
        return index < t->count ? t->members[index] : t->members[0];
    return t->element;
}

static uint32_t make_constant(Compiler *c, const DxValue *v)
{
    uint32_t type = dxil_type(c, v->type);
    switch (v->kind) {
    case DV_NULL:
    case DV_UNDEF: return spv_const_null(&c->b, type);
    case DV_INT:
    case DV_FLOAT: return scalar_constant(c, v->type, v->bits);
    case DV_AGGREGATE: {
        const DxType *t = &c->m.types[v->type];
        uint32_t count = v->count;
        uint32_t *parts = dxil_alloc(c, (count ? count : 1) * sizeof(uint32_t));
        if (!parts)
            return 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t et = element_type(c, v->type, i);
            parts[i] = v->data ? scalar_constant(c, et, v->data[i]) : dxil_value(c, v->ops[i]);
        }
        if (t->kind == DT_ARRAY)
            return spv_const_composite_array(&c->b, type, parts, count);
        return spv_const_composite(&c->b, type, parts, count);
    }
    default: dxil_fail(c, "a constant of an unsupported kind"); return 0;
    }
}

uint32_t dxil_value(Compiler *c, uint32_t id)
{
    Value *s = dxil_slot(c, id);
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (!s || !v) {
        dxil_fail(c, "a value out of range");
        return dxil_uconst(c, 0);
    }
    if (s->demoted && s->var)
        return spv_load(&c->b, s->type, s->var);
    if (s->kind == VAL_PLAIN && s->id)
        return s->id;
    switch (v->kind) {
    case DV_INT:
    case DV_FLOAT:
    case DV_NULL:
    case DV_UNDEF:
    case DV_AGGREGATE:
        s->kind = VAL_PLAIN;
        s->type = dxil_type(c, v->type);
        s->id = make_constant(c, v);
        return s->id;
    case DV_INSTRUCTION:
        if (s->kind == VAL_PARTS || s->kind == VAL_HANDLE || s->kind == VAL_POINTER)
            dxil_fail(c, "an aggregate used as a plain value");
        else
            dxil_fail(c, "a value used before its definition");
        return dxil_uconst(c, 0);
    default: dxil_fail(c, "a value of an unsupported kind"); return dxil_uconst(c, 0);
    }
}

uint64_t dxil_constant(Compiler *c, uint32_t id, uint64_t fallback)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (v && v->kind == DV_INT)
        return v->bits;
    if (v && v->kind == DV_NULL)
        return 0;
    return fallback;
}

/*
 * derivatives exist in pixel shaders, and in compute shaders made of whole
 * quads when the device supports it, a quad being four consecutive threads
 * as in direct3d.
 */
int dxil_derivatives(Compiler *c)
{
    if (c->kind == KIND_PIXEL)
        return 1;
    uint32_t threads = c->thread_group[0] * c->thread_group[1] * c->thread_group[2];
    if (c->kind != KIND_COMPUTE || !c->options->compute_derivatives || !threads || threads % 4)
        return 0;
    c->compute_derivatives = 1;
    return 1;
}

int dxil_is_constant(Compiler *c, uint32_t id)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    return v && (v->kind == DV_INT || v->kind == DV_NULL);
}

int dxil_is_undef(Compiler *c, uint32_t id)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    return !v || v->kind == DV_UNDEF;
}

void dxil_set_plain(Compiler *c, uint32_t id, uint32_t spv)
{
    Value *s = dxil_slot(c, id);
    if (!s)
        return;
    s->kind = VAL_PLAIN;
    s->id = spv;
    if (s->demoted && s->var)
        spv_store(&c->b, s->var, spv);
}

void dxil_set_parts(Compiler *c, uint32_t id, const uint32_t *parts, uint32_t count)
{
    Value *s = dxil_slot(c, id);
    if (!s)
        return;
    s->kind = VAL_PARTS;
    s->parts = dxil_alloc(c, (count ? count : 1) * sizeof(uint32_t));
    if (!s->parts)
        return;
    __builtin_memcpy(s->parts, parts, count * sizeof(uint32_t));
    s->part_count = count;
}

uint32_t dxil_extract(Compiler *c, uint32_t type, uint32_t value, uint32_t index)
{
    uint32_t w[2] = {value, index};
    return spv_op(&c->b, SpvOpCompositeExtract, type, w, 2);
}

uint32_t dxil_vector(Compiler *c, uint32_t scalar_type, const uint32_t *parts, uint32_t count)
{
    if (count == 1)
        return parts[0];
    return spv_op(&c->b, SpvOpCompositeConstruct, spv_type_vector(&c->b, scalar_type, count), parts, count);
}

/* bit width and floatness of a spir-v scalar type the compiler made. */
static uint32_t type_width(Compiler *c, uint32_t type, int *is_float)
{
    *is_float = type == c->t_f32 || type == c->t_f16 || type == c->t_f64;
    if (type == c->t_f16 || type == c->t_u16)
        return 16;
    if (type == c->t_f64 || type == c->t_u64)
        return 64;
    if (type == c->t_bool)
        return 1;
    return 32;
}

uint32_t dxil_convert(Compiler *c, uint32_t value, uint32_t from, uint32_t to)
{
    if (from == to)
        return value;
    int ff, tf;
    uint32_t fw = type_width(c, from, &ff), tw = type_width(c, to, &tf);
    if (fw == 1)
        return spv_op3(&c->b, SpvOpSelect, to, value, tf ? spv_const(&c->b, to, tw == 16 ? 0x3C00 : 0x3F800000) :
                                                             spv_const(&c->b, to, 1),
                       spv_const_null(&c->b, to));
    if (tw == 1)
        return spv_op2(&c->b, ff ? SpvOpFUnordNotEqual : SpvOpINotEqual, to, value, spv_const_null(&c->b, from));
    if (fw == tw)
        return spv_op1(&c->b, SpvOpBitcast, to, value);
    if (ff && tf)
        return spv_op1(&c->b, SpvOpFConvert, to, value);
    if (!ff && !tf)
        return spv_op1(&c->b, to == spv_type_int(&c->b, tw, 1) ? SpvOpSConvert : SpvOpUConvert, to, value);
    /* a float of another width goes through the integer of its own width. */
    uint32_t mid = ff ? spv_type_int(&c->b, fw, 0) : spv_type_float(&c->b, fw);
    if (ff)
        return spv_op1(&c->b, SpvOpUConvert, to, spv_op1(&c->b, SpvOpBitcast, mid, value));
    return spv_op1(&c->b, SpvOpBitcast, to, spv_op1(&c->b, SpvOpUConvert, spv_type_int(&c->b, tw, 0), value));
}

/* ------------------------------------------------------------------ pointers */

typedef struct Pointer {
    uint32_t id;
    /* the pointee's dxil and spir-v types. */
    uint32_t dx;
    uint32_t type;
    uint32_t storage;
} Pointer;

static uint32_t storage_of_space(Compiler *c, uint32_t space)
{
    switch (space) {
    case 0: return SpvStorageClassPrivate;
    case 3: return SpvStorageClassWorkgroup;
    default: dxil_fail(c, "a pointer into an unsupported address space"); return SpvStorageClassPrivate;
    }
}

static int pointer_of(Compiler *c, uint32_t id, Pointer *out);

/* how a dxil type is stored in groupshared memory. */
static uint32_t shared_type(Compiler *c, uint32_t t)
{
    if (c->shared_map[t])
        return c->shared_map[t];
    const DxType *ty = &c->m.types[t];
    uint32_t r = dxil_type(c, t);
    int f;
    switch (ty->kind) {
    case DT_HALF:
    case DT_FLOAT:
    case DT_DOUBLE:
        if (type_width(c, r, &f) == 64 && !c->t_u64) {
            spv_capability(&c->b, SpvCapabilityInt64);
            c->t_u64 = spv_type_int(&c->b, 64, 0);
        }
        r = type_width(c, r, &f) == 64 ? c->t_u64 : type_width(c, r, &f) == 16 ? type_u16(c) : c->t_u32;
        break;
    case DT_VECTOR: r = spv_type_vector(&c->b, shared_type(c, ty->element), ty->count); break;
    case DT_ARRAY: r = spv_type_array(&c->b, shared_type(c, ty->element), ty->count ? ty->count : 1); break;
    case DT_STRUCT: {
        uint32_t *members = dxil_alloc(c, (ty->count + 1) * sizeof(uint32_t));
        if (!members)
            return r;
        for (uint32_t i = 0; i < ty->count; i++)
            members[i] = shared_type(c, ty->members[i]);
        r = spv_type_struct(&c->b, members, ty->count);
        break;
    }
    default: break;
    }
    c->shared_map[t] = r;
    return r;
}

static uint32_t memory_type(Compiler *c, uint32_t t, uint32_t storage)
{
    return storage == SpvStorageClassWorkgroup ? shared_type(c, t) : dxil_type(c, t);
}

/* reinterprets a value of dxil type t bit for bit between memory types. */
static uint32_t reinterpret(Compiler *c, uint32_t value, uint32_t t, uint32_t from, uint32_t to)
{
    if (from == to)
        return value;
    const DxType *ty = &c->m.types[t];
    if (ty->kind != DT_ARRAY && ty->kind != DT_STRUCT)
        return spv_op1(&c->b, SpvOpBitcast, to, value);
    uint32_t n = ty->count ? ty->count : 1, *parts = dxil_alloc(c, (n + 1) * sizeof(uint32_t));
    if (!parts)
        return value;
    int shared = to != dxil_type(c, t);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = ty->kind == DT_ARRAY ? ty->element : ty->members[i];
        uint32_t plain = dxil_type(c, e), kept = shared_type(c, e);
        parts[i] = reinterpret(c, dxil_extract(c, shared ? plain : kept, value, i), e, shared ? plain : kept,
                               shared ? kept : plain);
    }
    return spv_op(&c->b, SpvOpCompositeConstruct, to, parts, n);
}

/* scalars of the same width may be read through another pointee with a bitcast. */
static int same_size(Compiler *c, uint32_t a, uint32_t b)
{
    int fa, fb;
    return a == b || (c->m.types[a].kind != DT_VECTOR && c->m.types[a].kind != DT_ARRAY &&
                      c->m.types[a].kind != DT_STRUCT && c->m.types[b].kind != DT_VECTOR &&
                      c->m.types[b].kind != DT_ARRAY && c->m.types[b].kind != DT_STRUCT &&
                      type_width(c, dxil_type(c, a), &fa) == type_width(c, dxil_type(c, b), &fb));
}

/* the first dxil index steps over the pointer itself. */
static int access(Compiler *c, const Pointer *base, const uint32_t *indices, uint32_t count, Pointer *out)
{
    if (!count) {
        *out = *base;
        return 0;
    }
    if (dxil_constant(c, indices[0], 1) != 0)
        return dxil_fail(c, "pointer arithmetic");
    if (count == 1) {
        *out = *base;
        return 0;
    }
    uint32_t t = base->dx;
    uint32_t *w = dxil_alloc(c, (count + 2) * sizeof(uint32_t));
    if (!w)
        return -1;
    w[0] = base->id;
    for (uint32_t i = 1; i < count; i++) {
        const DxType *ty = &c->m.types[t];
        if (ty->kind == DT_STRUCT) {
            uint64_t k = dxil_constant(c, indices[i], ~0ull);
            if (k >= ty->count)
                return dxil_fail(c, "a structure index that is not constant");
            t = ty->members[k];
        } else {
            t = ty->element;
        }
        w[i] = dxil_value(c, indices[i]);
    }
    out->dx = t;
    out->type = memory_type(c, t, base->storage);
    out->storage = base->storage;
    out->id = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, base->storage, out->type), w, count);
    return 0;
}

/* made at first use. */
static int global_pointer(Compiler *c, uint32_t id, const DxValue *v, Pointer *out)
{
    Value *s = dxil_slot(c, id);
    const DxType *pt = &c->m.types[v->type];
    if (pt->kind != DT_POINTER)
        return dxil_fail(c, "a global that is not a pointer");
    if (!s->id) {
        uint32_t storage = storage_of_space(c, pt->width);
        uint32_t type = memory_type(c, pt->element, storage);
        uint32_t init = 0;
        if (storage == SpvStorageClassPrivate && v->ops && v->ops[0] != DX_NONE) {
            const DxValue *iv = dx_value(&c->m, nullptr, v->ops[0]);
            if (iv && iv->kind != DV_UNDEF)
                init = dxil_value(c, v->ops[0]);
        }
        s->kind = VAL_POINTER;
        s->id = dxil_global(c, storage, type, init);
        s->type = type;
        s->storage = storage;
    }
    *out = (Pointer){s->id, pt->element, s->type, s->storage};
    return 0;
}

static int pointer_of(Compiler *c, uint32_t id, Pointer *out)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    Value *s = dxil_slot(c, id);
    if (!v || !s)
        return dxil_fail(c, "a pointer out of range");
    if (v->kind == DV_GLOBAL)
        return global_pointer(c, id, v, out);
    if (v->kind == DV_EXPR && v->sub == DX_EXPR_GEP && v->count) {
        Pointer base;
        if (pointer_of(c, v->ops[0], &base))
            return -1;
        return access(c, &base, v->ops + 1, v->count - 1, out);
    }
    if (v->kind == DV_EXPR && v->sub == DX_EXPR_CAST && v->count) {
        if (pointer_of(c, v->ops[0], out))
            return -1;
        const DxType *t = &c->m.types[v->type];
        if (t->kind == DT_POINTER && !same_size(c, t->element, out->dx))
            return dxil_fail(c, "a pointer cast to another type");
        return 0;
    }
    if (s->kind != VAL_POINTER)
        return dxil_fail(c, "a value used as a pointer");
    const DxType *t = &c->m.types[v->type];
    *out = (Pointer){s->id, t->kind == DT_POINTER ? t->element : 0, s->type, s->storage};
    return 0;
}

void dxil_set_pointer(Compiler *c, uint32_t id, uint32_t ptr, uint32_t type, uint32_t storage)
{
    Value *s = dxil_slot(c, id);
    if (!s)
        return;
    s->kind = VAL_POINTER;
    s->id = ptr;
    s->type = type;
    s->storage = storage;
}

uint32_t dxil_load_pointer(Compiler *c, uint32_t id, uint32_t *type)
{
    Pointer p;
    if (pointer_of(c, id, &p))
        return 0;
    *type = p.dx ? dxil_type(c, p.dx) : p.type;
    return reinterpret(c, spv_load(&c->b, p.type, p.id), p.dx, p.type, *type);
}

void dxil_store_pointer(Compiler *c, uint32_t id, uint32_t value)
{
    Pointer p;
    if (!pointer_of(c, id, &p))
        spv_store(&c->b, p.id, value);
}

static void set_pointer(Compiler *c, uint32_t id, const Pointer *p)
{
    Value *s = dxil_slot(c, id);
    if (!s)
        return;
    s->kind = VAL_POINTER;
    s->id = p->id;
    s->type = p->type;
    s->storage = p->storage;
}

static uint32_t memory_scope(const Pointer *p)
{
    return p->storage == SpvStorageClassWorkgroup ? SpvScopeWorkgroup : SpvScopeInvocation;
}

/* ------------------------------------------------------------------ instructions */

static uint32_t binop_opcode(Compiler *c, const DxInst *ins, int is_float, int is_bool)
{
    switch (ins->sub) {
    case DX_BIN_ADD: return is_float ? SpvOpFAdd : is_bool ? SpvOpLogicalNotEqual : SpvOpIAdd;
    case DX_BIN_SUB: return is_float ? SpvOpFSub : is_bool ? SpvOpLogicalNotEqual : SpvOpISub;
    case DX_BIN_MUL: return is_float ? SpvOpFMul : is_bool ? SpvOpLogicalAnd : SpvOpIMul;
    case DX_BIN_UDIV: return SpvOpUDiv;
    case DX_BIN_SDIV: return is_float ? SpvOpFDiv : SpvOpSDiv;
    case DX_BIN_UREM: return SpvOpUMod;
    case DX_BIN_SREM: return is_float ? SpvOpFRem : SpvOpSRem;
    case DX_BIN_SHL: return SpvOpShiftLeftLogical;
    case DX_BIN_LSHR: return SpvOpShiftRightLogical;
    case DX_BIN_ASHR: return SpvOpShiftRightArithmetic;
    case DX_BIN_AND: return is_bool ? SpvOpLogicalAnd : SpvOpBitwiseAnd;
    case DX_BIN_OR: return is_bool ? SpvOpLogicalOr : SpvOpBitwiseOr;
    case DX_BIN_XOR: return is_bool ? SpvOpLogicalNotEqual : SpvOpBitwiseXor;
    default: dxil_fail(c, "an unknown binary operation"); return SpvOpIAdd;
    }
}

static uint32_t signed_type(Compiler *c, uint32_t dx)
{
    const DxType *t = &c->m.types[dx];
    int f;
    uint32_t scalar = dxil_type(c, dxil_component_type(c, dx));
    uint32_t s = spv_type_int(&c->b, type_width(c, scalar, &f), 1);
    return t->kind == DT_VECTOR ? spv_type_vector(&c->b, s, t->count) : s;
}

static void binop(Compiler *c, const DxInst *ins)
{
    uint32_t type = dxil_type(c, ins->type);
    uint32_t a = dxil_value(c, ins->ops[0]), b = dxil_value(c, ins->ops[1]);
    uint32_t op = binop_opcode(c, ins, is_float_type(c, ins->type), is_bool_type(c, ins->type));
    uint32_t r;
    if (op == SpvOpSDiv || op == SpvOpShiftRightArithmetic) {
        /* on signed types, as drivers may take the result type's signedness for the operation's. */
        uint32_t st = signed_type(c, ins->type);
        r = spv_op2(&c->b, op, st, spv_op1(&c->b, SpvOpBitcast, st, a),
                    op == SpvOpShiftRightArithmetic ? b : spv_op1(&c->b, SpvOpBitcast, st, b));
        r = spv_op1(&c->b, SpvOpBitcast, type, r);
    } else if (op == SpvOpSRem) {
        /* a minus the truncated quotient times b, since nvidia's OpSRem of a negative a is positive. */
        uint32_t st = signed_type(c, ins->type);
        uint32_t sa = spv_op1(&c->b, SpvOpBitcast, st, a), sb = spv_op1(&c->b, SpvOpBitcast, st, b);
        uint32_t q = spv_op2(&c->b, SpvOpSDiv, st, sa, sb);
        r = spv_op1(&c->b, SpvOpBitcast, type, spv_op2(&c->b, SpvOpISub, st, sa, spv_op2(&c->b, SpvOpIMul, st, q, sb)));
    } else {
        r = spv_op2(&c->b, op, type, a, b);
    }
    if (ins->flags & DX_INST_PRECISE)
        spv_decorate(&c->b, r, SpvDecorationNoContraction, nullptr, 0);
    dxil_set_plain(c, ins->value, r);
}

/* 1 or all ones for integers, 1.0 or -1.0 for floats. */
static uint32_t from_bool(Compiler *c, uint32_t value, uint32_t to, int negative)
{
    int f;
    uint32_t w = type_width(c, to, &f);
    uint64_t one = f ? (w == 16 ? 0x3C00 : w == 64 ? 0x3FF0000000000000ull : 0x3F800000) : 1;
    if (negative)
        one = f ? one | (w == 16 ? 0x8000 : w == 64 ? 0x8000000000000000ull : 0x80000000) : ~0ull;
    uint32_t yes = w == 64 ? spv_const64(&c->b, to, one) : spv_const(&c->b, to, (uint32_t)one & (w == 16 ? 0xFFFF : ~0u));
    return spv_op3(&c->b, SpvOpSelect, to, value, yes, spv_const_null(&c->b, to));
}

/* vector casts keep their shape. */
static void cast(Compiler *c, const DxInst *ins)
{
    const DxValue *src = dx_value(&c->m, c->f, ins->ops[0]);
    if (!src)
        return (void)dxil_fail(c, "a cast of nothing");
    const DxType *to_dx = &c->m.types[ins->type];
    if (to_dx->kind == DT_POINTER) {
        Pointer p;
        if (pointer_of(c, ins->ops[0], &p))
            return;
        /* others, such as the i8 pointers lifetime markers take, fail where they are used. */
        if (same_size(c, to_dx->element, p.dx))
            set_pointer(c, ins->value, &p);
        return;
    }
    uint32_t to = dxil_type(c, ins->type), from = dxil_type(c, src->type);
    uint32_t v = dxil_value(c, ins->ops[0]);
    int from_bool_type = is_bool_type(c, src->type), to_bool_type = is_bool_type(c, ins->type);
    uint32_t r;
    switch (ins->sub) {
    case DX_CAST_TRUNC:
        if (to_bool_type)
            r = spv_op2(&c->b, SpvOpINotEqual, to,
                        spv_op2(&c->b, SpvOpBitwiseAnd, from, v, scalar_constant(c, src->type, 1)),
                        spv_const_null(&c->b, from));
        else
            r = from == to ? v : spv_op1(&c->b, SpvOpUConvert, to, v);
        break;
    case DX_CAST_ZEXT: r = from_bool_type ? from_bool(c, v, to, 0) : from == to ? v : spv_op1(&c->b, SpvOpUConvert, to, v); break;
    case DX_CAST_SEXT: r = from_bool_type ? from_bool(c, v, to, 1) : from == to ? v : spv_op1(&c->b, SpvOpSConvert, to, v); break;
    case DX_CAST_FPTOUI:
        r = to_bool_type ? spv_op2(&c->b, SpvOpFUnordNotEqual, to, v, spv_const_null(&c->b, from))
                         : spv_op1(&c->b, SpvOpConvertFToU, to, v);
        break;
    case DX_CAST_FPTOSI:
        r = to_bool_type ? spv_op2(&c->b, SpvOpFUnordNotEqual, to, v, spv_const_null(&c->b, from))
                         : spv_op1(&c->b, SpvOpConvertFToS, to, v);
        break;
    case DX_CAST_UITOFP: r = from_bool_type ? from_bool(c, v, to, 0) : spv_op1(&c->b, SpvOpConvertUToF, to, v); break;
    case DX_CAST_SITOFP: r = from_bool_type ? from_bool(c, v, to, 1) : spv_op1(&c->b, SpvOpConvertSToF, to, v); break;
    case DX_CAST_FPTRUNC:
    case DX_CAST_FPEXT: r = from == to ? v : spv_op1(&c->b, SpvOpFConvert, to, v); break;
    case DX_CAST_BITCAST: r = from == to ? v : spv_op1(&c->b, SpvOpBitcast, to, v); break;
    default: dxil_fail(c, "an unsupported cast"); return;
    }
    dxil_set_plain(c, ins->value, r);
}

static void compare(Compiler *c, const DxInst *ins)
{
    const DxValue *a_dx = dx_value(&c->m, c->f, ins->ops[0]);
    if (!a_dx)
        return (void)dxil_fail(c, "a comparison of nothing");
    uint32_t type = dxil_type(c, ins->type);
    uint32_t a = dxil_value(c, ins->ops[0]), b = dxil_value(c, ins->ops[1]);
    uint32_t op;
    int boolean = is_bool_type(c, a_dx->type);
    switch (ins->sub) {
    case DX_FCMP_FALSE: return dxil_set_plain(c, ins->value, spv_const_null(&c->b, type));
    case DX_FCMP_TRUE: {
        uint32_t t = spv_const_bool(&c->b, 1);
        const DxType *rt = &c->m.types[ins->type];
        if (rt->kind == DT_VECTOR) {
            uint32_t parts[4] = {t, t, t, t};
            t = spv_const_composite(&c->b, type, parts, rt->count);
        }
        return dxil_set_plain(c, ins->value, t);
    }
    case DX_FCMP_ORD:
    case DX_FCMP_UNO: {
        uint32_t na = spv_op1(&c->b, SpvOpIsNan, type, a), nb = spv_op1(&c->b, SpvOpIsNan, type, b);
        uint32_t any = spv_op2(&c->b, SpvOpLogicalOr, type, na, nb);
        return dxil_set_plain(c, ins->value, ins->sub == DX_FCMP_UNO ? any : spv_op1(&c->b, SpvOpLogicalNot, type, any));
    }
    case DX_FCMP_OEQ: op = SpvOpFOrdEqual; break;
    case DX_FCMP_OGT: op = SpvOpFOrdGreaterThan; break;
    case DX_FCMP_OGE: op = SpvOpFOrdGreaterThanEqual; break;
    case DX_FCMP_OLT: op = SpvOpFOrdLessThan; break;
    case DX_FCMP_OLE: op = SpvOpFOrdLessThanEqual; break;
    case DX_FCMP_ONE: op = SpvOpFOrdNotEqual; break;
    case DX_FCMP_UEQ: op = SpvOpFUnordEqual; break;
    case DX_FCMP_UGT: op = SpvOpFUnordGreaterThan; break;
    case DX_FCMP_UGE: op = SpvOpFUnordGreaterThanEqual; break;
    case DX_FCMP_ULT: op = SpvOpFUnordLessThan; break;
    case DX_FCMP_ULE: op = SpvOpFUnordLessThanEqual; break;
    case DX_FCMP_UNE: op = SpvOpFUnordNotEqual; break;
    case DX_ICMP_EQ: op = boolean ? SpvOpLogicalEqual : SpvOpIEqual; break;
    case DX_ICMP_NE: op = boolean ? SpvOpLogicalNotEqual : SpvOpINotEqual; break;
    case DX_ICMP_UGT: op = SpvOpUGreaterThan; break;
    case DX_ICMP_UGE: op = SpvOpUGreaterThanEqual; break;
    case DX_ICMP_ULT: op = SpvOpULessThan; break;
    case DX_ICMP_ULE: op = SpvOpULessThanEqual; break;
    case DX_ICMP_SGT: op = SpvOpSGreaterThan; break;
    case DX_ICMP_SGE: op = SpvOpSGreaterThanEqual; break;
    case DX_ICMP_SLT: op = SpvOpSLessThan; break;
    case DX_ICMP_SLE: op = SpvOpSLessThanEqual; break;
    default: dxil_fail(c, "an unknown comparison"); return;
    }
    if (boolean && op != SpvOpLogicalEqual && op != SpvOpLogicalNotEqual) {
        /* llvm orders i1 as unsigned, so compare through integers. */
        uint32_t u = c->t_u32;
        const DxType *at = &c->m.types[a_dx->type];
        if (at->kind == DT_VECTOR)
            u = spv_type_vector(&c->b, c->t_u32, at->count);
        a = from_bool(c, a, u, 0);
        b = from_bool(c, b, u, 0);
    }
    dxil_set_plain(c, ins->value, spv_op2(&c->b, op, type, a, b));
}

static void extract_element(Compiler *c, const DxInst *ins)
{
    uint32_t type = dxil_type(c, ins->type), vec = dxil_value(c, ins->ops[0]);
    uint64_t k = dxil_constant(c, ins->ops[1], ~0ull);
    uint32_t r = k != ~0ull ? dxil_extract(c, type, vec, (uint32_t)k)
                            : spv_op2(&c->b, SpvOpVectorExtractDynamic, type, vec, dxil_value(c, ins->ops[1]));
    dxil_set_plain(c, ins->value, r);
}

static void insert_element(Compiler *c, const DxInst *ins)
{
    uint32_t type = dxil_type(c, ins->type), vec = dxil_value(c, ins->ops[0]), v = dxil_value(c, ins->ops[1]);
    uint64_t k = dxil_constant(c, ins->ops[2], ~0ull);
    uint32_t r;
    if (k != ~0ull) {
        uint32_t w[3] = {v, vec, (uint32_t)k};
        r = spv_op(&c->b, SpvOpCompositeInsert, type, w, 3);
    } else {
        r = spv_op3(&c->b, SpvOpVectorInsertDynamic, type, vec, v, dxil_value(c, ins->ops[2]));
    }
    dxil_set_plain(c, ins->value, r);
}

static void shuffle(Compiler *c, const DxInst *ins)
{
    const DxValue *mask = dx_value(&c->m, c->f, ins->ops[2]);
    const DxType *rt = &c->m.types[ins->type];
    if (!mask || rt->kind != DT_VECTOR || rt->count > 16)
        return (void)dxil_fail(c, "an unsupported shuffle");
    uint32_t w[18] = {dxil_value(c, ins->ops[0]), dxil_value(c, ins->ops[1])};
    for (uint32_t i = 0; i < rt->count; i++) {
        uint32_t k = 0xFFFFFFFFu;
        if (mask->kind == DV_AGGREGATE) {
            if (mask->data)
                k = (uint32_t)mask->data[i];
            else if (mask->ops)
                k = (uint32_t)dxil_constant(c, mask->ops[i], 0xFFFFFFFFu);
        } else if (mask->kind == DV_NULL) {
            k = 0;
        }
        w[2 + i] = k;
    }
    dxil_set_plain(c, ins->value, spv_op(&c->b, SpvOpVectorShuffle, dxil_type(c, ins->type), w, 2 + rt->count));
}

static void extract_value(Compiler *c, const DxInst *ins)
{
    Value *s = dxil_slot(c, ins->ops[0]);
    if (!s || ins->count < 2)
        return (void)dxil_fail(c, "an extractvalue of nothing");
    if (s->kind == VAL_PARTS) {
        uint32_t k = ins->ops[1];
        if (k >= s->part_count || ins->count != 2)
            return (void)dxil_fail(c, "an extractvalue out of range");
        dxil_set_plain(c, ins->value, s->parts[k]);
        return;
    }
    uint32_t w[8] = {dxil_value(c, ins->ops[0])};
    if (ins->count > 8)
        return (void)dxil_fail(c, "an extractvalue too deep");
    for (uint32_t i = 1; i < ins->count; i++)
        w[i] = ins->ops[i];
    dxil_set_plain(c, ins->value, spv_op(&c->b, SpvOpCompositeExtract, dxil_type(c, ins->type), w, ins->count));
}

static void insert_value(Compiler *c, const DxInst *ins)
{
    const DxType *t = &c->m.types[ins->type];
    if (t->kind != DT_STRUCT || ins->count != 3 || ins->ops[2] >= t->count)
        return (void)dxil_fail(c, "an unsupported insertvalue");
    uint32_t parts[32];
    Value *s = dxil_slot(c, ins->ops[0]);
    for (uint32_t i = 0; i < t->count && i < 32; i++)
        parts[i] = s && s->kind == VAL_PARTS && i < s->part_count ? s->parts[i]
                                                                   : spv_const_null(&c->b, dxil_type(c, t->members[i]));
    parts[ins->ops[2]] = dxil_value(c, ins->ops[1]);
    dxil_set_parts(c, ins->value, parts, t->count < 32 ? t->count : 32);
}

static void load(Compiler *c, const DxInst *ins)
{
    Pointer p;
    if (dxil_resource_global(c, ins) || pointer_of(c, ins->ops[0], &p))
        return;
    uint32_t type = dxil_type(c, ins->type);
    dxil_set_plain(c, ins->value, reinterpret(c, spv_load(&c->b, p.type, p.id), ins->type, p.type, type));
}

static void store(Compiler *c, const DxInst *ins)
{
    Pointer p;
    if (pointer_of(c, ins->ops[0], &p))
        return;
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[1]);
    uint32_t value = dxil_value(c, ins->ops[1]);
    if (v)
        value = reinterpret(c, value, v->type, dxil_type(c, v->type), p.type);
    spv_store(&c->b, p.id, value);
}

static void gep(Compiler *c, const DxInst *ins)
{
    Pointer base, out;
    if (dxil_resource_global(c, ins) || pointer_of(c, ins->ops[0], &base) || access(c, &base, ins->ops + 1, ins->count - 1, &out))
        return;
    set_pointer(c, ins->value, &out);
}

static void atomic_rmw(Compiler *c, const DxInst *ins)
{
    Pointer p;
    if (pointer_of(c, ins->ops[0], &p))
        return;
    uint32_t op;
    switch (ins->sub) {
    case DX_RMW_XCHG: op = SpvOpAtomicExchange; break;
    case DX_RMW_ADD: op = SpvOpAtomicIAdd; break;
    case DX_RMW_SUB: op = SpvOpAtomicISub; break;
    case DX_RMW_AND: op = SpvOpAtomicAnd; break;
    case DX_RMW_OR: op = SpvOpAtomicOr; break;
    case DX_RMW_XOR: op = SpvOpAtomicXor; break;
    case DX_RMW_MAX: op = SpvOpAtomicSMax; break;
    case DX_RMW_MIN: op = SpvOpAtomicSMin; break;
    case DX_RMW_UMAX: op = SpvOpAtomicUMax; break;
    case DX_RMW_UMIN: op = SpvOpAtomicUMin; break;
    default: dxil_fail(c, "an unsupported atomic operation"); return;
    }
    if (p.type == c->t_u64)
        spv_capability(&c->b, SpvCapabilityInt64Atomics);
    uint32_t type = dxil_type(c, ins->type);
    uint32_t w[4] = {p.id, dxil_uconst(c, memory_scope(&p)), dxil_uconst(c, 0),
                     dxil_convert(c, dxil_value(c, ins->ops[1]), type, p.type)};
    dxil_set_plain(c, ins->value, dxil_convert(c, spv_op(&c->b, op, p.type, w, 4), p.type, type));
}

static void cmpxchg(Compiler *c, const DxInst *ins)
{
    Pointer p;
    if (pointer_of(c, ins->ops[0], &p))
        return;
    if (p.type == c->t_u64)
        spv_capability(&c->b, SpvCapabilityInt64Atomics);
    if (p.type == c->t_f32 || p.type == c->t_f16 || p.type == c->t_f64)
        return (void)dxil_fail(c, "a compare exchange of floats");
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[1]);
    uint32_t type = v ? dxil_type(c, v->type) : p.type;
    uint32_t cmp = dxil_convert(c, dxil_value(c, ins->ops[1]), type, p.type);
    uint32_t w[6] = {p.id, dxil_uconst(c, memory_scope(&p)), dxil_uconst(c, 0), dxil_uconst(c, 0),
                     dxil_convert(c, dxil_value(c, ins->ops[2]), type, p.type), cmp};
    uint32_t old = spv_op(&c->b, SpvOpAtomicCompareExchange, p.type, w, 6);
    uint32_t parts[2] = {dxil_convert(c, old, p.type, type), spv_op2(&c->b, SpvOpIEqual, c->t_bool, old, cmp)};
    dxil_set_parts(c, ins->value, parts, 2);
}

static void select_value(Compiler *c, const DxInst *ins)
{
    const DxType *t = &c->m.types[ins->type];
    if (t->kind == DT_POINTER || is_handle_type(c, ins->type))
        return (void)dxil_fail(c, "a select of pointers or handles");
    uint32_t r = spv_op3(&c->b, SpvOpSelect, dxil_type(c, ins->type), dxil_value(c, ins->ops[0]),
                         dxil_value(c, ins->ops[1]), dxil_value(c, ins->ops[2]));
    dxil_set_plain(c, ins->value, r);
}

static const char *callee_name(Compiler *c, const DxInst *ins)
{
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[0]);
    if (!v || v->kind != DV_FUNCTION || v->inst >= c->m.function_count)
        return nullptr;
    return c->m.functions[v->inst].name;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return 0;
    return 1;
}

static void call(Compiler *c, const DxInst *ins)
{
    const char *name = callee_name(c, ins);
    if (!name)
        return (void)dxil_fail(c, "a call of an unknown function");
    if (starts_with(name, "llvm.lifetime.") || starts_with(name, "dx.noop") || starts_with(name, "llvm.dbg."))
        return;
    if (!starts_with(name, "dx.op.") || ins->count < 2)
        return (void)dxil_fail(c, "a call of a function that is not dx.op");
    uint64_t op = dxil_constant(c, ins->ops[1], ~0ull);
    if (op == ~0ull)
        return (void)dxil_fail(c, "a dx.op without its opcode");
    int r = dxil_io_op(c, ins, (uint32_t)op);
    if (!r)
        r = dxil_resource_op(c, ins, (uint32_t)op);
    if (!r)
        r = dxil_math_op(c, ins, (uint32_t)op);
    if (!r)
        r = dxil_ray_op(c, ins, (uint32_t)op);
    if (!r)
        dxil_fail(c, "a dx.op not implemented");
}

static void instruction(Compiler *c, const DxInst *ins)
{
    switch (ins->op) {
    case DI_BINOP: binop(c, ins); break;
    case DI_CAST: cast(c, ins); break;
    case DI_GEP: gep(c, ins); break;
    case DI_SELECT: select_value(c, ins); break;
    case DI_EXTRACTELT: extract_element(c, ins); break;
    case DI_INSERTELT: insert_element(c, ins); break;
    case DI_SHUFFLEVEC: shuffle(c, ins); break;
    case DI_CMP: compare(c, ins); break;
    case DI_ALLOCA: break;
    case DI_LOAD: load(c, ins); break;
    case DI_STORE: store(c, ins); break;
    case DI_EXTRACTVAL: extract_value(c, ins); break;
    case DI_INSERTVAL: insert_value(c, ins); break;
    case DI_CALL: call(c, ins); break;
    case DI_ATOMICRMW: atomic_rmw(c, ins); break;
    case DI_CMPXCHG: cmpxchg(c, ins); break;
    default: dxil_fail(c, "an unsupported instruction"); break;
    }
}

/* ------------------------------------------------------------------ inner conditionals */

void dxil_begin_if(Compiler *c, uint32_t cond, uint32_t *merge)
{
    uint32_t then = spv_id(&c->b);
    *merge = spv_id(&c->b);
    uint32_t w[2] = {*merge, SpvSelectionControlMaskNone};
    spv_op_void(&c->b, SpvOpSelectionMerge, w, 2);
    spv_branch_conditional(&c->b, cond, then, *merge);
    spv_label(&c->b, then);
}

void dxil_end_if(Compiler *c, uint32_t merge)
{
    spv_branch(&c->b, merge);
    spv_label(&c->b, merge);
}

/* ------------------------------------------------------------------ functions */

static int orphan_block(const Cfg *cfg, uint32_t b) { return cfg->blocks[b].idom == DX_NONE; }

/* the cfg block defining a value used in block u. */
static uint32_t definition_block(Compiler *c, const uint32_t *phi_block, uint32_t inst, uint32_t u)
{
    const CfgBlock *ub = &c->cfg->blocks[u];
    uint32_t s = c->f->insts[inst].block;
    if (ub->copy && ub->source == s)
        return u;
    if (phi_block[inst] != DX_NONE)
        return phi_block[inst];
    return s + 1;
}

/* demotes a value whose definition does not dominate its use in block u. */
static void check_use(Compiler *c, const uint32_t *phi_block, uint32_t id, uint32_t u, int depth)
{
    if (id >= CFG_SYNTH || depth > 8)
        return;
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (!v || v->kind != DV_INSTRUCTION)
        return;
    const DxInst *ins = &c->f->insts[v->inst];
    if (ins->op == DI_CALL && is_handle_type(c, ins->type)) {
        /* handles are lowered where used, so their operands count as used there. */
        for (uint32_t i = 1; i < ins->count; i++)
            check_use(c, phi_block, ins->ops[i], u, depth + 1);
        return;
    }
    uint32_t d = definition_block(c, phi_block, v->inst, u);
    if (d == u || cfg_dominates(c->cfg, d, u))
        return;
    Value *s = dxil_slot(c, id);
    if (s)
        s->demoted = 1;
}

static void check_instruction(Compiler *c, const uint32_t *phi_block, const DxInst *ins, uint32_t u)
{
    switch (ins->op) {
    case DI_PHI:
    case DI_ALLOCA: return;
    case DI_BR:
        if (ins->count == 3)
            check_use(c, phi_block, ins->ops[2], u, 0);
        return;
    case DI_SWITCH: check_use(c, phi_block, ins->ops[0], u, 0); return;
    case DI_EXTRACTVAL: check_use(c, phi_block, ins->ops[0], u, 0); return;
    case DI_INSERTVAL:
        check_use(c, phi_block, ins->ops[0], u, 0);
        check_use(c, phi_block, ins->ops[1], u, 0);
        return;
    default:
        for (uint32_t i = 0; i < ins->count; i++)
            check_use(c, phi_block, ins->ops[i], u, 0);
        return;
    }
}

static int find_demotions(Compiler *c)
{
    const Cfg *cfg = c->cfg;
    const DxFunction *f = c->f;
    uint32_t *phi_block = dxil_alloc(c, (f->inst_count + 1) * sizeof(uint32_t));
    if (!phi_block)
        return -1;
    for (uint32_t i = 0; i < f->inst_count; i++)
        phi_block[i] = DX_NONE;
    for (uint32_t i = 0; i < cfg->order_count; i++) {
        uint32_t b = cfg->order[i];
        const CfgBlock *blk = &cfg->blocks[b];
        if (blk->copy || orphan_block(cfg, b))
            continue;
        for (uint32_t k = 0; k < blk->phi_count; k++) {
            const DxValue *v = blk->phis[k].result < CFG_SYNTH ? dx_value(&c->m, f, blk->phis[k].result) : nullptr;
            if (v && v->kind == DV_INSTRUCTION)
                phi_block[v->inst] = b;
        }
    }
    for (uint32_t i = 0; i < cfg->order_count; i++) {
        uint32_t b = cfg->order[i];
        const CfgBlock *blk = &cfg->blocks[b];
        if (orphan_block(cfg, b))
            continue;
        if (blk->source != DX_NONE) {
            const DxBlock *db = &f->blocks[blk->source];
            for (uint32_t k = 0; k + 1 < db->count; k++)
                check_instruction(c, phi_block, &f->insts[db->first + k], b);
        }
        if ((blk->term == TERM_COND || blk->term == TERM_SWITCH) && blk->cond < CFG_SYNTH)
            check_use(c, phi_block, blk->cond, b, 0);
        for (uint32_t k = 0; k < blk->phi_count; k++)
            for (uint32_t j = 0; j < blk->phis[k].count; j++)
                check_use(c, phi_block, blk->phis[k].values[j], blk->phis[k].preds[j], 0);
    }
    /* what can live in a variable. */
    for (uint32_t i = 0; i < f->value_count; i++) {
        Value *s = &c->values[i];
        if (!s->demoted)
            continue;
        const DxValue *v = &f->values[i];
        const DxType *t = &c->m.types[v->type];
        if (t->kind == DT_POINTER || t->kind == DT_STRUCT)
            return dxil_fail(c, "a pointer or aggregate whose definition does not dominate its use");
        s->type = dxil_type(c, v->type);
    }
    return c->error ? -1 : 0;
}

/* allocas and demoted values, at the start of the first block. */
static void make_variables(Compiler *c)
{
    const DxFunction *f = c->f;
    for (uint32_t i = 0; i < f->inst_count; i++) {
        const DxInst *ins = &f->insts[i];
        if (ins->op != DI_ALLOCA)
            continue;
        const DxType *pt = &c->m.types[ins->type];
        uint32_t element = pt->kind == DT_POINTER ? pt->element : ins->type;
        uint32_t type = dxil_type(c, element);
        uint64_t count = ins->count ? dxil_constant(c, ins->ops[0], 1) : 1;
        if (count != 1)
            type = spv_type_array(&c->b, type, (uint32_t)count);
        uint32_t storage = SpvStorageClassFunction;
        uint32_t var = spv_op(&c->b, SpvOpVariable, spv_type_pointer(&c->b, storage, type), &storage, 1);
        Value *s = dxil_slot(c, ins->value);
        if (s) {
            s->kind = VAL_POINTER;
            s->id = var;
            s->type = type;
            s->storage = storage;
        }
    }
    for (uint32_t i = 0; i < f->value_count; i++) {
        Value *s = &c->values[i];
        if (!s->demoted)
            continue;
        uint32_t storage = SpvStorageClassFunction;
        s->var = spv_op(&c->b, SpvOpVariable, spv_type_pointer(&c->b, storage, s->type), &storage, 1);
    }
}

typedef struct PhiTable {
    uint32_t *first;
    size_t *word;
    uint32_t *values, *labels;
} PhiTable;

static uint32_t phi_operand(Compiler *c, uint32_t value, uint32_t type)
{
    if (value == CFG_UNDEF)
        return spv_const_null(&c->b, dxil_type(c, type));
    if (value >= CFG_CONST)
        return dxil_uconst(c, value - CFG_CONST);
    if (value >= CFG_SYNTH)
        return c->synth[value - CFG_SYNTH];
    return dxil_value(c, value);
}

/* the operands the successors' phis take from block b, read before its terminator. */
static void resolve_phis(Compiler *c, PhiTable *t, uint32_t b)
{
    const Cfg *cfg = c->cfg;
    const CfgBlock *blk = &cfg->blocks[b];
    for (uint32_t i = 0; i < blk->succ_count; i++) {
        uint32_t s = blk->succ[i];
        int seen = 0;
        for (uint32_t k = 0; k < i; k++)
            seen |= blk->succ[k] == s;
        if (seen)
            continue;
        const CfgBlock *sb = &cfg->blocks[s];
        uint32_t at = t->first[s];
        for (uint32_t k = 0; k < sb->phi_count; k++) {
            const CfgPhi *p = &sb->phis[k];
            for (uint32_t j = 0; j < p->count; j++)
                if (p->preds[j] == b) {
                    t->values[at + j] = phi_operand(c, p->values[j], p->type);
                    t->labels[at + j] = c->block_end[b];
                }
            at += p->count;
        }
    }
}

static uint32_t condition(Compiler *c, uint32_t value)
{
    if (value >= CFG_SYNTH)
        return phi_operand(c, value, 0);
    return dxil_value(c, value);
}

static void terminator(Compiler *c, uint32_t b)
{
    const CfgBlock *blk = &c->cfg->blocks[b];
    /* the condition may be a load, which has to come before the merge: a merge is second to last in its block. */
    uint32_t cond = blk->term == TERM_COND || blk->term == TERM_SWITCH ? condition(c, blk->cond) : 0;
    if (blk->header == HEADER_LOOP) {
        uint32_t w[3] = {c->labels[blk->merge], c->labels[blk->cont], SpvLoopControlMaskNone};
        spv_op_void(&c->b, SpvOpLoopMerge, w, 3);
    } else if (blk->header == HEADER_SELECTION) {
        uint32_t w[2] = {c->labels[blk->merge], SpvSelectionControlMaskNone};
        spv_op_void(&c->b, SpvOpSelectionMerge, w, 2);
    }
    switch (blk->term) {
    case TERM_BRANCH: spv_branch(&c->b, c->labels[blk->succ[0]]); break;
    case TERM_COND:
        spv_branch_conditional(&c->b, cond, c->labels[blk->succ[0]], c->labels[blk->succ[1]]);
        break;
    case TERM_SWITCH: {
        uint32_t selector = cond;
        int wide = 0;
        if (blk->cond < CFG_SYNTH) {
            const DxValue *v = dx_value(&c->m, c->f, blk->cond);
            wide = v && dxil_type(c, v->type) == c->t_u64;
        }
        uint32_t count = blk->succ_count - 1;
        uint32_t *w = dxil_alloc(c, (2 + count * 3) * sizeof(uint32_t));
        if (!w)
            return;
        uint32_t n = 0;
        w[n++] = selector;
        w[n++] = c->labels[blk->succ[0]];
        for (uint32_t i = 0; i < count; i++) {
            w[n++] = (uint32_t)blk->cases[i];
            if (wide)
                w[n++] = (uint32_t)(blk->cases[i] >> 32);
            w[n++] = c->labels[blk->succ[1 + i]];
        }
        spv_op_void(&c->b, SpvOpSwitch, w, n);
        c->b.block_open = 0;
        break;
    }
    case TERM_RETURN:
        if (c->kind == KIND_AMPLIFICATION)
            dxil_emit_tasks(c);
        else
            spv_return(&c->b);
        break;
    default:
        /* ignoring a hit or ending the search already ended the block. */
        if (c->b.block_open)
            spv_op_void(&c->b, SpvOpUnreachable, nullptr, 0);
        c->b.block_open = 0;
        break;
    }
}

static int emit_block(Compiler *c, PhiTable *t, uint32_t b)
{
    const Cfg *cfg = c->cfg;
    const CfgBlock *blk = &cfg->blocks[b];
    c->current = b;
    spv_label(&c->b, c->labels[b]);
    if (orphan_block(cfg, b)) {
        spv_op_void(&c->b, SpvOpUnreachable, nullptr, 0);
        c->b.block_open = 0;
        return 0;
    }
    if (b == cfg->entry)
        make_variables(c);
    SpvSection *code = &c->b.sections[SECTION_FUNCTIONS];
    uint32_t at = t->first[b];
    for (uint32_t k = 0; k < blk->phi_count; k++) {
        const CfgPhi *p = &blk->phis[k];
        if (p->result < CFG_SYNTH && is_handle_type(c, p->type))
            return dxil_fail(c, "a phi of handles");
        uint32_t id = spv_id(&c->b), type = dxil_type(c, p->type);
        t->word[at] = code->count;
        uint32_t *w = dxil_alloc(c, (2 + 2 * p->count) * sizeof(uint32_t));
        if (!w)
            return -1;
        w[0] = type;
        w[1] = id;
        spv_emit(&c->b, SECTION_FUNCTIONS, SpvOpPhi, w, 2 + 2 * p->count);
        if (p->result >= CFG_SYNTH)
            c->synth[p->result - CFG_SYNTH] = id;
        else {
            Value *s = dxil_slot(c, p->result);
            if (s) {
                s->kind = VAL_PLAIN;
                s->id = id;
                s->type = type;
            }
        }
        at += p->count;
    }
    /* demoted phi results go to their variables after every phi. */
    for (uint32_t k = 0; k < blk->phi_count; k++) {
        Value *s = blk->phis[k].result < CFG_SYNTH ? dxil_slot(c, blk->phis[k].result) : nullptr;
        if (s && s->demoted && s->var)
            spv_store(&c->b, s->var, s->id);
    }
    if (blk->source != DX_NONE) {
        const DxBlock *db = &c->f->blocks[blk->source];
        for (uint32_t k = 0; k + 1 < db->count && !c->error; k++) {
            const DxInst *ins = &c->f->insts[db->first + k];
            if (ins->op != DI_PHI)
                instruction(c, ins);
        }
    }
    c->block_end[b] = c->b.block;
    resolve_phis(c, t, b);
    terminator(c, b);
    return c->error ? -1 : 0;
}

static int compile_function(Compiler *c, const DxFunction *f, uint32_t id)
{
    c->f = f;
    c->values = dxil_alloc(c, (f->value_count + 1) * sizeof(Value));
    c->cfg = dxil_alloc(c, sizeof(Cfg));
    if (!c->values || !c->cfg)
        return -1;
    if (dxil_is_ray_stage(c->kind) && dxil_ray_arguments(c, f))
        return -1;
    if (cfg_build(c->cfg, &c->m, f, &c->arena))
        return dxil_fail(c, c->cfg->error[0] ? c->cfg->error : "control flow");
    const Cfg *cfg = c->cfg;
    c->synth = dxil_alloc(c, (cfg->synth_count + 1) * sizeof(uint32_t));
    c->labels = dxil_alloc(c, cfg->count * sizeof(uint32_t));
    c->block_end = dxil_alloc(c, cfg->count * sizeof(uint32_t));
    PhiTable t;
    t.first = dxil_alloc(c, (cfg->count + 1) * sizeof(uint32_t));
    if (!c->synth || !c->labels || !c->block_end || !t.first)
        return -1;
    uint32_t operands = 0;
    for (uint32_t b = 0; b < cfg->count; b++) {
        c->labels[b] = spv_id(&c->b);
        t.first[b] = operands;
        for (uint32_t k = 0; k < cfg->blocks[b].phi_count; k++)
            operands += cfg->blocks[b].phis[k].count;
    }
    t.first[cfg->count] = operands;
    t.word = dxil_alloc(c, (operands + 1) * sizeof(size_t));
    t.values = dxil_alloc(c, (operands + 1) * sizeof(uint32_t));
    t.labels = dxil_alloc(c, (operands + 1) * sizeof(uint32_t));
    if (!t.word || !t.values || !t.labels)
        return -1;
    if (find_demotions(c))
        return -1;
    c->part_masks = dxil_alloc(c, (f->value_count + 1) * sizeof(uint32_t));
    if (!c->part_masks)
        return -1;
    for (uint32_t i = 0; i < f->inst_count; i++) {
        const DxInst *ins = &f->insts[i];
        if (ins->op == DI_BR || ins->op == DI_SWITCH)
            continue;
        uint32_t used = ins->op == DI_EXTRACTVAL ? 1 : ins->op == DI_INSERTVAL ? 2 : ins->count;
        if (ins->op == DI_EXTRACTVAL && ins->count >= 2 && ins->ops[0] >= c->m.value_count) {
            uint32_t k = ins->ops[0] - c->m.value_count;
            if (k < f->value_count)
                c->part_masks[k] |= ins->ops[1] < 32 ? 1u << ins->ops[1] : ~0u;
            continue;
        }
        for (uint32_t j = 0; j < used; j++)
            if (ins->ops[j] >= c->m.value_count && ins->ops[j] - c->m.value_count < f->value_count)
                c->part_masks[ins->ops[j] - c->m.value_count] = ~0u;
    }

    spv_function_begin(&c->b, id, c->t_void, spv_type_function(&c->b, c->t_void, nullptr, 0));
    for (uint32_t i = 0; i < cfg->order_count; i++)
        if (emit_block(c, &t, cfg->order[i]))
            return -1;
    spv_function_end(&c->b);

    /* phi operands, now that every block is written. */
    SpvSection *code = &c->b.sections[SECTION_FUNCTIONS];
    for (uint32_t i = 0; i < cfg->order_count && !c->b.failed; i++) {
        uint32_t b = cfg->order[i];
        const CfgBlock *blk = &cfg->blocks[b];
        if (orphan_block(cfg, b))
            continue;
        uint32_t at = t.first[b];
        for (uint32_t k = 0; k < blk->phi_count; k++) {
            const CfgPhi *p = &blk->phis[k];
            uint32_t *w = code->words + t.word[at] + 3;
            for (uint32_t j = 0; j < p->count; j++) {
                if (!t.values[at + j] || !t.labels[at + j])
                    return dxil_fail(c, "a phi operand from a block never written");
                w[2 * j] = t.values[at + j];
                w[2 * j + 1] = t.labels[at + j];
            }
            at += p->count;
        }
    }
    return c->error ? -1 : 0;
}

/* ------------------------------------------------------------------ the entry point */

/* dxc mangles library names as msvc does, "\01?name@@" plus the signature, and direct3d exports the bare name. */
static size_t demangled(const char *name, const char **start)
{
    if (name[0] == 1)
        name++;
    if (name[0] != '?') {
        *start = name;
        return __builtin_strlen(name);
    }
    name++;
    size_t n = 0;
    while (name[n] && name[n] != '@')
        n++;
    *start = name;
    return n;
}

int dxil_export_matches(const char *function, const char *export_name)
{
    if (!function)
        return 0;
    const char *start;
    size_t n = demangled(function, &start);
    return __builtin_strlen(export_name) == n && !__builtin_memcmp(start, export_name, n);
}

static const DxFunction *function_of(Compiler *c, const DxMetadata *md)
{
    if (!md || md->kind != DM_VALUE)
        return nullptr;
    for (uint32_t i = 0; i < c->m.function_count; i++)
        if (c->m.functions[i].value == md->value && !c->m.functions[i].prototype)
            return &c->m.functions[i];
    return nullptr;
}

/* entry point properties: flags and each stage's description. */
static int read_properties(Compiler *c, const DxMetadata *list)
{
    if (!list)
        return 0;
    for (uint32_t i = 0; i + 1 < list->count; i += 2) {
        uint64_t tag = dx_md_int(&c->m, list, i, ~0ull);
        const DxMetadata *v = dx_md_op(&c->m, list, i + 1);
        switch (tag) {
        case 0: c->flags = dx_md_int(&c->m, list, i + 1, 0); break;
        case 1:
            if (v) {
                c->gs_input = (uint32_t)dx_md_int(&c->m, v, 0, 0);
                c->gs_max_vertices = (uint32_t)dx_md_int(&c->m, v, 1, 1);
                c->gs_streams = (uint32_t)dx_md_int(&c->m, v, 2, 1);
                c->gs_topology = (uint32_t)dx_md_int(&c->m, v, 3, 0);
                c->gs_instances = (uint32_t)dx_md_int(&c->m, v, 4, 1);
            }
            break;
        case 2:
            if (v) {
                c->tess_domain = (uint32_t)dx_md_int(&c->m, v, 0, 0);
                c->cp_in = (uint32_t)dx_md_int(&c->m, v, 1, 0);
            }
            break;
        case 3:
            if (v) {
                c->patch_function = function_of(c, dx_md_op(&c->m, v, 0));
                c->cp_in = (uint32_t)dx_md_int(&c->m, v, 1, 0);
                c->cp_out = (uint32_t)dx_md_int(&c->m, v, 2, 0);
                c->tess_domain = (uint32_t)dx_md_int(&c->m, v, 3, 0);
                c->tess_partitioning = (uint32_t)dx_md_int(&c->m, v, 4, 0);
                c->tess_primitive = (uint32_t)dx_md_int(&c->m, v, 5, 0);
                const DxMetadata *factor = dx_md_op(&c->m, v, 6);
                const DxValue *fv = factor && factor->kind == DM_VALUE ? dx_value(&c->m, nullptr, factor->value) : nullptr;
                c->max_tess_factor = fv && fv->kind == DV_FLOAT ? (uint32_t)fv->bits : 0;
            }
            break;
        case 4:
            if (v)
                for (uint32_t k = 0; k < 3; k++)
                    c->thread_group[k] = (uint32_t)dx_md_int(&c->m, v, k, 1);
            break;
        case 9:
            /* the mesh shader: threads, vertices, primitives, the topology and the payload's size. */
            if (v) {
                for (uint32_t k = 0; k < 3; k++)
                    c->thread_group[k] = (uint32_t)dx_md_int(&c->m, dx_md_op(&c->m, v, 0), k, 1);
                c->mesh_vertices = (uint32_t)dx_md_int(&c->m, v, 1, 0);
                c->mesh_primitives = (uint32_t)dx_md_int(&c->m, v, 2, 0);
                c->mesh_topology = (uint32_t)dx_md_int(&c->m, v, 3, 2);
            }
            break;
        case 10:
            /* the amplification shader: threads and the payload's size. */
            if (v)
                for (uint32_t k = 0; k < 3; k++)
                    c->thread_group[k] = (uint32_t)dx_md_int(&c->m, dx_md_op(&c->m, v, 0), k, 1);
            break;
        case 11:
            /* shader model 6.6's WaveSize: one size. */
            if (v)
                c->wave_min = c->wave_max = c->wave_preferred = (uint32_t)dx_md_int(&c->m, v, 0, 0);
            break;
        case 23:
            /* shader model 6.8's WaveSize: the smallest, the largest and the preferred size. */
            if (v) {
                c->wave_min = (uint32_t)dx_md_int(&c->m, v, 0, 0);
                c->wave_max = (uint32_t)dx_md_int(&c->m, v, 1, 0);
                c->wave_preferred = (uint32_t)dx_md_int(&c->m, v, 2, 0);
            }
            break;
        case 8:
            /* a library's function: its shader kind. */
            if (c->kind == KIND_LIBRARY)
                c->kind = (uint32_t)dx_md_int(&c->m, list, i + 1, KIND_LIBRARY);
            break;
        default: break;
        }
    }
    return 0;
}

static int build(Compiler *c)
{
    SpvBuilder *b = &c->b;
    spv_capability(b, SpvCapabilityShader);
    spv_capability(b, SpvCapabilityRuntimeDescriptorArray);
    make_types(c);
    c->type_map = dxil_alloc(c, (c->m.type_count + 64) * sizeof(uint32_t));
    c->shared_map = dxil_alloc(c, (c->m.type_count + 64) * sizeof(uint32_t));
    c->module_values = dxil_alloc(c, (c->m.value_count + 1) * sizeof(Value));
    if (!c->type_map || !c->shared_map || !c->module_values)
        return -1;
    const DxNamedMetadata *entries = dx_named(&c->m, "dx.entryPoints");
    if (!entries || !entries->count || entries->nodes[0] >= c->m.metadata_count)
        return dxil_fail(c, "no entry point");
    const DxMetadata *entry = &c->m.metadata[entries->nodes[0]];
    const DxMetadata *resources = nullptr;
    if (c->kind == KIND_LIBRARY) {
        /* a library export uses the whole library's resources. */
        entry = nullptr;
        for (uint32_t i = 0; i < entries->count && !entry; i++) {
            const DxMetadata *md = entries->nodes[i] < c->m.metadata_count ? &c->m.metadata[entries->nodes[i]] : nullptr;
            const DxFunction *fn = function_of(c, dx_md_op(&c->m, md, 0));
            if (fn && c->options->export_name && dxil_export_matches(fn->name, c->options->export_name))
                entry = md;
        }
        if (!entry)
            return dxil_fail(c, "no such export in the library");
        const DxNamedMetadata *named = dx_named(&c->m, "dx.resources");
        if (named && named->count && named->nodes[0] < c->m.metadata_count)
            resources = &c->m.metadata[named->nodes[0]];
    } else {
        resources = dx_md_op(&c->m, entry, 3);
    }
    const DxFunction *main_fn = function_of(c, dx_md_op(&c->m, entry, 0));
    if (!main_fn)
        return dxil_fail(c, "no entry function");
    if (read_properties(c, dx_md_op(&c->m, entry, 4)))
        return -1;
    if (c->kind == KIND_LIBRARY || (c->kind > KIND_COMPUTE && c->kind < KIND_MESH && !dxil_is_ray_stage(c->kind)))
        return dxil_fail(c, "a library function of a kind not supported");
    if (dxil_is_ray_stage(c->kind)) {
        spv_capability(b, SpvCapabilityRayTracingKHR);
        spv_extension(b, "SPV_KHR_ray_tracing");
    }
    c->native16 = (c->flags >> 23) & 1;
    if (c->native16) {
        type_f16(c);
        type_u16(c);
    }
    if (dxil_read_signatures(c, dx_md_op(&c->m, entry, 2)) || dxil_read_resources(c, resources))
        return -1;
    if (c->kind == KIND_MESH || c->kind == KIND_AMPLIFICATION) {
        spv_capability(b, SpvCapabilityMeshShadingEXT);
        spv_extension(b, "SPV_EXT_mesh_shader");
        if (c->kind == KIND_AMPLIFICATION && dxil_prepare_task(c, main_fn))
            return -1;
    }
    dxil_prepare_ray_queries(c, main_fn);
    if (c->kind == KIND_PIXEL && dxil_prepare_vertex_inputs(c, main_fn))
        return -1;
    c->residency = c->options->residency && dxil_uses_op(c, main_fn, 71);

    uint32_t main = spv_id(b);
    if (c->kind == KIND_HULL) {
        if (!c->patch_function)
            return dxil_fail(c, "a hull shader without its patch constant function");
        uint32_t cp = spv_id(b), pc = spv_id(b);
        if (compile_function(c, main_fn, cp))
            return -1;
        c->in_patch_function = 1;
        if (compile_function(c, c->patch_function, pc))
            return -1;
        c->in_patch_function = 0;
        /* every invocation writes its control point; the first then writes the patch's constants. */
        c->f = nullptr;
        uint32_t void_fn = spv_type_function(b, c->t_void, nullptr, 0);
        spv_function_begin(b, main, c->t_void, void_fn);
        spv_label(b, spv_id(b));
        spv_op(b, SpvOpFunctionCall, c->t_void, &cp, 1);
        uint32_t barrier[3] = {dxil_uconst(c, SpvScopeWorkgroup), dxil_uconst(c, SpvScopeInvocation), dxil_uconst(c, 0)};
        spv_op_void(b, SpvOpControlBarrier, barrier, 3);
        uint32_t i32 = spv_type_int(b, 32, 1);
        uint32_t id = spv_load(b, i32, dxil_builtin(c, SpvBuiltInInvocationId, SpvStorageClassInput, i32));
        uint32_t merge;
        dxil_begin_if(c, spv_op2(b, SpvOpIEqual, c->t_bool, id, spv_const(b, i32, 0)), &merge);
        spv_op(b, SpvOpFunctionCall, c->t_void, &pc, 1);
        dxil_end_if(c, merge);
        spv_function_end(b);
    } else if (compile_function(c, main_fn, main)) {
        return -1;
    }
    if (c->error)
        return -1;
    static const uint32_t MODELS[] = {[KIND_PIXEL] = SpvExecutionModelFragment,
                                      [KIND_VERTEX] = SpvExecutionModelVertex,
                                      [KIND_GEOMETRY] = SpvExecutionModelGeometry,
                                      [KIND_HULL] = SpvExecutionModelTessellationControl,
                                      [KIND_DOMAIN] = SpvExecutionModelTessellationEvaluation,
                                      [KIND_COMPUTE] = SpvExecutionModelGLCompute,
                                      [KIND_RAYGEN] = SpvExecutionModelRayGenerationKHR,
                                      [KIND_INTERSECTION] = SpvExecutionModelIntersectionKHR,
                                      [KIND_ANYHIT] = SpvExecutionModelAnyHitKHR,
                                      [KIND_CLOSESTHIT] = SpvExecutionModelClosestHitKHR,
                                      [KIND_MISS] = SpvExecutionModelMissKHR,
                                      [KIND_CALLABLE] = SpvExecutionModelCallableKHR,
                                      [KIND_MESH] = SpvExecutionModelMeshEXT,
                                      [KIND_AMPLIFICATION] = SpvExecutionModelTaskEXT};
    uint32_t model[2] = {MODELS[c->kind], main};
    spv_emit_string(b, SECTION_ENTRY_POINTS, SpvOpEntryPoint, model, 2, "main", c->interface, c->interface_count);
    dxil_execution_modes(c, main);
    return b->failed ? dxil_fail(c, "out of memory") : c->error ? -1 : 0;
}

int dxil_library_exports(const void *bytecode, size_t length, DxilExport *out, uint32_t max,
                         const MimeAllocator *alloc)
{
    uint32_t size;
    const uint8_t *part = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('D', 'X', 'I', 'L'), &size);
    if (!part || size < 24 || (*(const uint32_t *)part >> 16) != KIND_LIBRARY)
        return -1;
    uint32_t offset = *(const uint32_t *)(part + 16), bc_size = *(const uint32_t *)(part + 20);
    if (offset > size - 8 || bc_size > size - 8 - offset)
        return -1;
    Compiler *c = mime_alloc(alloc, sizeof(*c));
    if (!c)
        return -1;
    __builtin_memset(c, 0, sizeof(*c));
    int count = -1;
    if (!dx_module_read(&c->m, part + 8 + offset, bc_size, alloc)) {
        count = 0;
        const DxNamedMetadata *entries = dx_named(&c->m, "dx.entryPoints");
        for (uint32_t i = 0; entries && i < entries->count; i++) {
            const DxMetadata *md = entries->nodes[i] < c->m.metadata_count ? &c->m.metadata[entries->nodes[i]] : nullptr;
            const DxFunction *fn = function_of(c, dx_md_op(&c->m, md, 0));
            if (!fn || !fn->name)
                continue;
            if ((uint32_t)count < max) {
                DxilExport *e = &out[count];
                __builtin_memset(e, 0, sizeof(*e));
                const char *start;
                size_t n = demangled(fn->name, &start);
                if (n >= sizeof(e->name))
                    n = sizeof(e->name) - 1;
                __builtin_memcpy(e->name, start, n);
                const DxMetadata *props = dx_md_op(&c->m, md, 4);
                for (uint32_t k = 0; props && k + 1 < props->count; k += 2) {
                    uint64_t tag = dx_md_int(&c->m, props, k, ~0ull), value = dx_md_int(&c->m, props, k + 1, 0);
                    if (tag == 8)
                        e->kind = (uint32_t)value;
                    else if (tag == 6)
                        e->payload_size = (uint32_t)value;
                    else if (tag == 7)
                        e->attribute_size = (uint32_t)value;
                }
            }
            count++;
        }
    }
    dx_module_free(&c->m);
    mime_free(alloc, c);
    return count;
}

int dxil_is(const void *bytecode, size_t length)
{
    uint32_t size;
    return dxbc_find_chunk(bytecode, length, DXBC_FOURCC('D', 'X', 'I', 'L'), &size) != nullptr;
}

static void set_error(DxbcShaderInfo *info, const char *error)
{
    size_t n = __builtin_strlen(error);
    if (n >= sizeof(info->error))
        n = sizeof(info->error) - 1;
    __builtin_memcpy(info->error, error, n);
    info->error[n] = 0;
}

int dxil_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc)
{
    __builtin_memset(info, 0, sizeof(*info));
    uint32_t size;
    const uint8_t *part = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('D', 'X', 'I', 'L'), &size);
    /* program header: version and kind, size, then the bitcode header. */
    if (!part || size < 24) {
        set_error(info, "no dxil part");
        return -1;
    }
    uint32_t version = *(const uint32_t *)part;
    uint32_t offset = *(const uint32_t *)(part + 16), bc_size = *(const uint32_t *)(part + 20);
    if (*(const uint32_t *)(part + 8) != DXBC_FOURCC('D', 'X', 'I', 'L') || offset > size - 8 ||
        bc_size > size - 8 - offset) {
        set_error(info, "a malformed dxil part");
        return -1;
    }
    uint32_t kind = version >> 16;
    info->program = (DxbcProgram)kind;
    info->major = (version >> 4) & 0xF;
    info->minor = version & 0xF;
    if (kind > KIND_COMPUTE && kind != KIND_MESH && kind != KIND_AMPLIFICATION &&
        !(kind == KIND_LIBRARY && options && options->export_name)) {
        set_error(info, "a shader kind not supported");
        return -1;
    }
    dxbc_parse_io(bytecode, length, 0, &info->inputs, alloc);
    dxbc_parse_io(bytecode, length, 1, &info->outputs, alloc);
    dxbc_parse_signature(bytecode, length, DXBC_FOURCC('P', 'S', 'G', '1'), &info->patch_constants, alloc);

    Compiler *c = mime_alloc(alloc, sizeof(*c));
    if (!c) {
        set_error(info, "out of memory");
        return -1;
    }
    __builtin_memset(c, 0, sizeof(*c));
    c->alloc = alloc;
    c->info = info;
    c->options = options;
    c->kind = kind;
    c->arena.alloc = alloc;
    spv_init(&c->b, alloc);
    int result = -1;
    if (!options || !options->root_signature) {
        dxil_fail(c, "dxil without a root signature");
    } else if (dx_module_read(&c->m, part + 8 + offset, bc_size, alloc)) {
        set_error(info, c->m.error[0] ? c->m.error : "malformed bitcode");
        dx_module_free(&c->m);
        spv_free(&c->b);
        mime_free(alloc, c);
        return -1;
    } else {
        result = build(c);
        dx_module_free(&c->m);
    }
    if (!result)
        info->spirv = spv_finish(&c->b, &info->spirv_words);
    if (result || !info->spirv) {
        set_error(info, c->error ? c->error : "out of memory");
        result = -1;
    }
    info->program = (DxbcProgram)c->kind;
    info->wave_min = c->wave_min;
    info->wave_max = c->wave_max ? c->wave_max : c->wave_min;
    info->wave_preferred = c->wave_preferred;
    info->thread_group[0] = c->thread_group[0];
    info->thread_group[1] = c->thread_group[1];
    info->thread_group[2] = c->thread_group[2];
    bc_arena_free(&c->arena);
    spv_free(&c->b);
    mime_free(alloc, c);
    return result;
}
